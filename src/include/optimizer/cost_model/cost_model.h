#pragma once

#include <algorithm>
#include <vector>

#include "common/macros.h"
#include "parser/expression/column_value_expression.h"
#include "optimizer/cost_model/abstract_cost_model.h"
#include "optimizer/memo.h"
#include "optimizer/physical_operators.h"
#include "optimizer/statistics/stats_storage.h"
#include "optimizer/statistics/table_stats.h"
#include "transaction/transaction_context.h"

namespace terrier::optimizer {

class Memo;
class GroupExpression;

/**
 * Cost model based on the PostgreSQL cost model formulas.
 */
class CostModel : public AbstractCostModel {
 public:
  /**
   * Default constructor
   */
  CostModel() = default;

  /**
   * Costs a GroupExpression
   * @param txn TransactionContext that query is generated under
   * @param memo Memo object containing all relevant groups
   * @param gexpr GroupExpression to calculate cost for
   */
  double CalculateCost(transaction::TransactionContext *txn, catalog::CatalogAccessor *accessor, Memo *memo,
                       GroupExpression *gexpr) override {
    gexpr_ = gexpr;
    memo_ = memo;
    txn_ = txn;
    gexpr_->Contents()->Accept(common::ManagedPointer<OperatorVisitor>(this));
    return output_cost_;
  };

  /**
   * Visit a SeqScan operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const SeqScan *op) override {
    auto table_stats = stats_storage_->GetTableStats(op->GetDatabaseOID(), op->GetTableOID());
    if (table_stats->GetColumnCount() == 0) {
      output_cost_ = 1.f;
      return;
    }
    output_cost_ = table_stats->GetNumRows() * tuple_cpu_cost;
  }

  /**
   * Visit a IndexScan operator
   * @param op operator
   */
  void Visit(const IndexScan *op) override {
    auto table_stats = stats_storage_->GetTableStats(op->GetDatabaseOID(), op->GetTableOID());
    if (table_stats->GetColumnCount() == 0 || table_stats->GetNumRows() == 0) {
      output_cost_ = 0.f;
      return;
    }
    output_cost_ = std::log2(table_stats->GetNumRows()) * tuple_cpu_cost +
                   memo_->GetGroupByID(gexpr_->GetGroupID())->GetNumRows() * tuple_cpu_cost;
  }

  /**
   * Visit a QueryDerivedScan operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const QueryDerivedScan *op) override { output_cost_ = 0.f; }

  /**
   * Visit a OrderBy operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const OrderBy *op) override { output_cost_ = 0.f; }

  /**
   * Visit a Limit operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const Limit *op) override {
    auto child_num_rows = memo_->GetGroupByID(gexpr_->GetChildGroupId(0))->GetNumRows();
    output_cost_ = std::min(static_cast<size_t>(child_num_rows), static_cast<size_t>(op->GetLimit())) * tuple_cpu_cost;
  }

  /**
   * Visit a InnerIndexJoin operator
   * @param op operator
   */
  void Visit(const InnerIndexJoin *op) override {}

  /**
   * Visit a InnerNLJoin operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const InnerNLJoin *op) override {
    double outer_rows = memo_->GetGroupByID(gexpr_->GetChildGroupId(0))->GetNumRows();
    double inner_rows = memo_->GetGroupByID(gexpr_->GetChildGroupId(1))->GetNumRows();
    auto total_row_count = memo_->GetGroupByID(gexpr_->GetGroupID())->GetNumRows();

    double init_cost = 0.0;
    //line 2704
    if (outer_rows > 1) {
      init_cost += (outer_rows - 1) * (tuple_cpu_cost * inner_rows);
    }

    //line 2740
    // automatically set row counts to 1 if given counts aren't valid
    if (outer_rows <= 0) {
      outer_rows = 1;
    }

    if (inner_rows <= 0) {
      inner_rows = 1;
    }

    double num_tuples;
    double total_cpu_cost_per_tuple;

    //line 2885
    // cases are computed by simply considering all tuple pairs
    num_tuples = outer_rows * inner_rows;

    //line 2891 : cpu_per_tuple
    // compute cpu cost per tuple
    // formula: cpu cost for evaluating all qualifier clauses for the join per tuple + cpu cost to emit tuple
    total_cpu_cost_per_tuple =
        GetCPUCostForQuals(const_cast<std::vector<AnnotatedExpression> &&>(op->GetJoinPredicates())) + tuple_cpu_cost;

    //line 2892,2896,2899
    // calculate total cpu cost for all tuples
    output_cost_ = init_cost + num_tuples * total_cpu_cost_per_tuple + tuple_cpu_cost * total_row_count;
  }

  /**
   * Visit a LeftNLJoin operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const LeftNLJoin *op) override {}

  /**
   * Visit a RightNLJoin operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const RightNLJoin *op) override {}

  /**
   * Visit a OuterNLJoin operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const OuterNLJoin *op) override {}

  /**
   * Visit a InnerHashJoin operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const InnerHashJoin *op) override {
    // get num rows for both tables that are being joined
    // left child cols should be inserted in the hash table, while right is hashed to check for equality
    double left_rows = memo_->GetGroupByID(gexpr_->GetChildGroupId(0))->GetNumRows();
    double right_rows = memo_->GetGroupByID(gexpr_->GetChildGroupId(1))->GetNumRows();
    auto total_row_count = memo_->GetGroupByID(gexpr_->GetGroupID())->GetNumRows();

    double init_cost = 0.0;
    //left = outer & right = inner
    //line 3524
    init_cost += (op_cpu_cost * op->GetJoinPredicates().size() + tuple_cpu_cost) * right_rows;
    init_cost += op_cpu_cost * op->GetJoinPredicates().size() * left_rows;

    auto left_table_oid = op->GetLeftKeys()[0].CastManagedPointerTo<parser::ColumnValueExpression>()->GetTableOid();

    double frac_null;
    double num_distinct;
    double avg_freq;

    // overall saved estimations
    auto bucket_size_frac = 1.0;
    auto mcv_freq = 1.0;
    // line 3674
    for (const auto &pred : op->GetJoinPredicates()) {
      // current estimated stats on left table
      double curr_bucket_size_frac;
      double curr_mcv_freq;

      auto curr_left_child = pred.GetExpr()->GetChild(0).CastManagedPointerTo<parser::ColumnValueExpression>();
      auto curr_right_child = pred.GetExpr()->GetChild(1).CastManagedPointerTo<parser::ColumnValueExpression>();
      auto curr_left_table_oid = curr_left_child->GetTableOid();

      //line 3688
      common::ManagedPointer<ColumnStats> col_stats;
      if (curr_left_table_oid == left_table_oid) {
        col_stats = stats_storage_->GetTableStats(curr_left_child->GetDatabaseOid(), curr_left_child->GetTableOid())
                        ->GetColumnStats(curr_left_child->GetColumnOid());
      } else {
        col_stats = stats_storage_->GetTableStats(curr_right_child->GetDatabaseOid(), curr_right_child->GetTableOid())
                        ->GetColumnStats(curr_right_child->GetColumnOid());
      }
      //line 3696
      // using the stats of the column referred to in the join predicate
      // estimate # of buckets for each ht: cardinality * 2 (mock real hash table which aims for load factor of 0.5)
      double buckets = col_stats->GetCardinality() * 2;
      curr_mcv_freq = col_stats->GetCommonFreqs()[0];
      num_distinct = col_stats->GetCardinality();
      frac_null = col_stats->GetFracNull();
      avg_freq = (1.0 - frac_null) / num_distinct;

      // get ratio of col rows with restrict clauses applied over all possible rows (w/o restrictions)
      auto overall_col_ratio = total_row_count / std::max(left_rows, right_rows);

      if (total_row_count > 0) {
        num_distinct *= overall_col_ratio;
        if (num_distinct < 1.0) {
          num_distinct = 1.0;
        } else {
          num_distinct = uint32_t(num_distinct);
        }
      }

      //line 3667
      if (num_distinct > buckets) {
        curr_bucket_size_frac = 1.0 / buckets;
      } else {
        curr_bucket_size_frac = 1.0 / num_distinct;
      }

      if (avg_freq > 0.0 && curr_mcv_freq > avg_freq) {
        curr_bucket_size_frac *= curr_mcv_freq / avg_freq;
      }

      if (curr_bucket_size_frac < 1.0e-6) {
        curr_bucket_size_frac = 1.0e-6;
      } else if (curr_bucket_size_frac > 1.0) {
        curr_bucket_size_frac = 1.0;
      }

      if (bucket_size_frac > curr_bucket_size_frac) {
        bucket_size_frac = curr_bucket_size_frac;
      }

      //line 3724
      if (mcv_freq > curr_mcv_freq) {
        mcv_freq = curr_mcv_freq;
      }
    }

    //line 3749
    auto hash_cost = GetCPUCostForQuals(const_cast<std::vector<AnnotatedExpression> &&>(op->GetJoinPredicates()));
    //line 3818
    auto row_est = right_rows * bucket_size_frac * 0.5;
    if (row_est < 1.0) {
      row_est = 1.0;
    } else {
      row_est = uint32_t(row_est);
    }
    //line 3818, 3841
    output_cost_ = init_cost + hash_cost * left_rows * row_est * 0.5 + tuple_cpu_cost * total_row_count;
  }
  /**
   * Visit a LeftHashJoin operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const LeftHashJoin *op) override {}

  /**
   * Visit a RightHashJoin operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const RightHashJoin *op) override {}

  /**
   * Visit a OuterHashJoin operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const OuterHashJoin *op) override {}

  /**
   * Visit a Insert operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const Insert *op) override {}

  /**
   * Visit a InsertSelect operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const InsertSelect *op) override {}

  /**
   * Visit a Delete operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const Delete *op) override {}

  /**
   * Visit a Update operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const Update *op) override {}

  /**
   * Visit a HashGroupBy operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const HashGroupBy *op) override { output_cost_ = 0.f; }

  /**
   * Visit a SortGroupBy operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const SortGroupBy *op) override { output_cost_ = 1.f; }

  /**
   * Visit a Aggregate operator
   * @param op operator
   */
  void Visit(UNUSED_ATTRIBUTE const Aggregate *op) override { output_cost_ = 0.f; }

  /**
   * Sets stats storage variable
   * @param storage StatsStorage object
   */
  void SetStatsStorage(StatsStorage *storage) { stats_storage_ = storage; }

 private:
  /**
   * Calculates the CPU cost (for one tuple) to evaluate all qualifiers
   * @param qualifiers - list of qualifiers to be evaluated
   * @return CPU cost
   */
  double GetCPUCostForQuals(std::vector<AnnotatedExpression> &&qualifiers) {
    auto total_cost = 1.f;
    for (const auto &q : qualifiers) {
      total_cost += GetCPUCostPerQual(q.GetExpr());
    }
    return total_cost;
  }

  /**
  * Calculates the CPU cost for one qualifier
  * @param qualifier - qualifer to calculate cost for
  * @return cost of qualifier
  */
  double GetCPUCostPerQual(common::ManagedPointer<parser::AbstractExpression> qualifier) {
    auto qual_type = qualifier->GetExpressionType();
    auto total_cost = 1.f;
    if (qual_type == parser::ExpressionType::FUNCTION) {
      // TODO(viv): find out how to calculate cost of function
    } else if (qual_type == parser::ExpressionType::OPERATOR_UNARY_MINUS ||  // not really proud of this ...
               qual_type == parser::ExpressionType::OPERATOR_PLUS ||
               qual_type == parser::ExpressionType::OPERATOR_MINUS ||
               qual_type == parser::ExpressionType::OPERATOR_MULTIPLY ||
               qual_type == parser::ExpressionType::OPERATOR_DIVIDE ||
               qual_type == parser::ExpressionType::OPERATOR_CONCAT ||
               qual_type == parser::ExpressionType::OPERATOR_MOD ||
               qual_type == parser::ExpressionType::OPERATOR_CAST ||
               qual_type == parser::ExpressionType::OPERATOR_IS_NULL ||
               qual_type == parser::ExpressionType::OPERATOR_IS_NOT_NULL ||
               qual_type == parser::ExpressionType::OPERATOR_EXISTS ||
               qual_type == parser::ExpressionType::OPERATOR_NULL_IF ||
               qual_type == parser::ExpressionType::COMPARE_EQUAL) {
      total_cost += op_cpu_cost;
    }
    for (const auto &c : qualifier->GetChildren()) {
      total_cost += GetCPUCostPerQual(c);
    }
    // TODO(viv): add more casing to cost other expr types
    return total_cost;
  }

  /**
   * Statistics storage object for all tables
   */
  StatsStorage *stats_storage_;

  /**
   * GroupExpression to cost
   */
  GroupExpression *gexpr_;

  /**
   * Memo table to use
   */
  Memo *memo_;

  /**
   * Transaction Context
   */
  transaction::TransactionContext *txn_;

  /**
   * CPU cost to materialize a tuple
   * TODO(viv): change later to be evaluated per instantiation via a benchmark
   */
  double tuple_cpu_cost = 2.f;

  /**
   * Cost to execute an operator
   * TODO(viv): find a better constant for op cost (?)
   */
  double op_cpu_cost = 2.f;

  /**
   * Computed output cost
   */
  double output_cost_ = 0.f;
};

}  // namespace terrier::optimizer
