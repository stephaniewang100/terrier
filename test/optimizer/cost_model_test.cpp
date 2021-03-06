#include <utility>
#include <algorithm>
#include <vector>

#include "optimizer/cost_model/cost_model.h"
#include "execution/compiler/expression_maker.h"
#include "gtest/gtest.h"
#include "optimizer/operator_node.h"
#include "optimizer/optimizer_context.h"
#include "optimizer/optimizer_defs.h"
#include "optimizer/physical_operators.h"
#include "optimizer/statistics/histogram.h"
#include "optimizer/statistics/top_k_elements.h"

#include "test_util/test_harness.h"

namespace terrier::optimizer {
class CostModelTests : public TerrierTest {
 protected:
  static constexpr size_t NUM_ROWS_A = 100'000;
  static constexpr size_t NUM_ROWS_B = 5;
  static constexpr size_t NUM_ROWS_C = 1'000;
  static constexpr size_t NUM_ROWS_D = 100;
  static constexpr size_t NUM_ROWS_E = 100'000;

  ColumnStats column_stats_obj_a_1_;
  ColumnStats column_stats_obj_b_1_;
  ColumnStats column_stats_obj_c_1_;
  ColumnStats column_stats_obj_d_1_;
  ColumnStats column_stats_obj_e_1_;

  TableStats table_stats_obj_a_;
  TableStats table_stats_obj_b_;
  TableStats table_stats_obj_c_;
  TableStats table_stats_obj_d_;
  TableStats table_stats_obj_e_;

  StatsStorage stats_storage_;
  CostModel cost_model_;
  void SetUp() override {
    /////// COLUMNS //////
    // table 1 column stats
    column_stats_obj_a_1_ = ColumnStats(catalog::db_oid_t(1), catalog::table_oid_t(1), catalog::col_oid_t(1),
                                        NUM_ROWS_A, NUM_ROWS_A / 2.0, 0.2, {1, 2, 3}, {5, 5, 5}, {1.0, 5.0}, true);

    // table 2 column stats
    column_stats_obj_b_1_ = ColumnStats(catalog::db_oid_t(1), catalog::table_oid_t(2), catalog::col_oid_t(1),
                                        NUM_ROWS_B, NUM_ROWS_B, 0.0, {3, 4, 5}, {2, 2, 2}, {1.0, 5.0}, true);

    // table 3 column stats
    column_stats_obj_c_1_ = ColumnStats(catalog::db_oid_t(1), catalog::table_oid_t(3), catalog::col_oid_t(1),
                                        NUM_ROWS_C, NUM_ROWS_C, 0.0, {3, 4, 5}, {2, 2, 2}, {1.0, 5.0}, true);

    // table 4 column stats
    column_stats_obj_d_1_ = ColumnStats(catalog::db_oid_t(1), catalog::table_oid_t(4), catalog::col_oid_t(1),
                                        NUM_ROWS_D, NUM_ROWS_D, 0.0, {3, 4, 5}, {2, 2, 2}, {1.0, 5.0}, true);

    // table 5 column stats
    column_stats_obj_e_1_ = ColumnStats(catalog::db_oid_t(1), catalog::table_oid_t(5), catalog::col_oid_t(1),
                                        NUM_ROWS_E, NUM_ROWS_E / 2.0, 0.0, {3, 4, 5}, {2, 2, 2}, {1.0, 5.0}, true);

    ////// TABLES //////
    \
    // table 1
    table_stats_obj_a_ =
        TableStats(catalog::db_oid_t(1), catalog::table_oid_t(1), NUM_ROWS_A, true, {column_stats_obj_a_1_});
    // table 2
    table_stats_obj_b_ =
        TableStats(catalog::db_oid_t(1), catalog::table_oid_t(2), NUM_ROWS_B, true, {column_stats_obj_b_1_});
    // table 3
    table_stats_obj_c_ =
        TableStats(catalog::db_oid_t(1), catalog::table_oid_t(3), NUM_ROWS_C, true, {column_stats_obj_c_1_});
    // table 4
    table_stats_obj_d_ =
        TableStats(catalog::db_oid_t(1), catalog::table_oid_t(4), NUM_ROWS_D, true, {column_stats_obj_d_1_});
    // table 5
    table_stats_obj_e_ =
        TableStats(catalog::db_oid_t(1), catalog::table_oid_t(5), NUM_ROWS_E, true, {column_stats_obj_e_1_});

    stats_storage_ = StatsStorage();
    stats_storage_.InsertTableStats(catalog::db_oid_t(1), catalog::table_oid_t(1), std::move(table_stats_obj_a_));
    stats_storage_.InsertTableStats(catalog::db_oid_t(1), catalog::table_oid_t(2), std::move(table_stats_obj_b_));
    stats_storage_.InsertTableStats(catalog::db_oid_t(1), catalog::table_oid_t(3), std::move(table_stats_obj_c_));
    stats_storage_.InsertTableStats(catalog::db_oid_t(1), catalog::table_oid_t(4), std::move(table_stats_obj_d_));
    stats_storage_.InsertTableStats(catalog::db_oid_t(1), catalog::table_oid_t(5), std::move(table_stats_obj_e_));

    cost_model_ = CostModel();
    cost_model_.SetStatsStorage(&stats_storage_);
  }
};

// NOLINTNEXTLINE
TEST_F(CostModelTests, InnerNLJoinCorrectnessTest1) {
  OptimizerContext context_((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_.SetStatsStorage(&stats_storage_);
  // create child gexprs
  auto seq_scan_1 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(1),
                                  std::vector<AnnotatedExpression>(), "table", false);
  auto seq_scan_2 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(2),
                                  std::vector<AnnotatedExpression>(), "table", false);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_larger_outer = {};
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));

  Operator inner_nl_join_a_first = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_first = OperatorNode(inner_nl_join_a_first, std::move(children_larger_outer), nullptr);
  auto gexpr_inner_nl_join =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_first));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join, false);
  auto left_group = context_.GetMemo().GetGroupByID(group_id_t(0));
  auto right_group = context_.GetMemo().GetGroupByID(group_id_t(1));
  auto curr_group = context_.GetMemo().GetGroupByID(group_id_t(2));
  left_group->SetNumRows(table_stats_obj_a_.GetNumRows());
  right_group->SetNumRows(table_stats_obj_b_.GetNumRows());
  curr_group->SetNumRows(1000);
  auto left_gexpr = left_group->GetPhysicalExpressions()[0];
  auto right_gexpr = right_group->GetPhysicalExpressions()[0];
  auto left_prop_set = new PropertySet();
  auto right_prop_set = new PropertySet();

  left_group->SetExpressionCost(left_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), left_gexpr),
                                left_prop_set);
  right_group->SetExpressionCost(right_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), right_gexpr),
                                 right_prop_set);
  auto cost_larger_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_smaller_outer = {};
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));

  Operator inner_nl_join_a_second = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_second = OperatorNode(inner_nl_join_a_second, std::move(children_smaller_outer), nullptr);
  auto gexpr_inner_nl_join_2 =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_second));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join_2, false);
  auto curr_group_2 = context_.GetMemo().GetGroupByID(group_id_t(3));
  curr_group_2->SetNumRows(1000);
  auto cost_smaller_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join_2);
  //should be less than
  EXPECT_LT(cost_smaller_outer, cost_larger_outer);
}

TEST_F(CostModelTests, InnerNLJoinCorrectnessTest2) {
  OptimizerContext context_((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_.SetStatsStorage(&stats_storage_);
  // create child gexprs
  auto seq_scan_1 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(3),
                                  std::vector<AnnotatedExpression>(), "table", false);
  auto seq_scan_2 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(4),
                                  std::vector<AnnotatedExpression>(), "table", false);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_larger_outer = {};
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));

  Operator inner_nl_join_a_first = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_first = OperatorNode(inner_nl_join_a_first, std::move(children_larger_outer), nullptr);
  auto gexpr_inner_nl_join =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_first));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join, false);
  auto left_group = context_.GetMemo().GetGroupByID(group_id_t(0));
  auto right_group = context_.GetMemo().GetGroupByID(group_id_t(1));
  auto curr_group = context_.GetMemo().GetGroupByID(group_id_t(2));
  left_group->SetNumRows(table_stats_obj_c_.GetNumRows());
  right_group->SetNumRows(table_stats_obj_d_.GetNumRows());
  curr_group->SetNumRows(1000);
  auto left_gexpr = left_group->GetPhysicalExpressions()[0];
  auto right_gexpr = right_group->GetPhysicalExpressions()[0];
  auto left_prop_set = new PropertySet();
  auto right_prop_set = new PropertySet();

  left_group->SetExpressionCost(left_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), left_gexpr),
                                left_prop_set);
  right_group->SetExpressionCost(right_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), right_gexpr),
                                 right_prop_set);
  auto cost_larger_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_smaller_outer = {};
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));

  Operator inner_nl_join_a_second = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_second = OperatorNode(inner_nl_join_a_second, std::move(children_smaller_outer), nullptr);
  auto gexpr_inner_nl_join_2 =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_second));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join_2, false);
  auto curr_group_2 = context_.GetMemo().GetGroupByID(group_id_t(3));
  curr_group_2->SetNumRows(1000);
  auto cost_smaller_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join_2);
  //should be less than
  EXPECT_LT(cost_smaller_outer, cost_larger_outer);
}

TEST_F(CostModelTests, InnerNLJoinCorrectnessTest3) {
  OptimizerContext context_((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_.SetStatsStorage(&stats_storage_);
  // create child gexprs
  auto seq_scan_1 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(1),
                                  std::vector<AnnotatedExpression>(), "table", false);
  auto seq_scan_2 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(3),
                                  std::vector<AnnotatedExpression>(), "table", false);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_larger_outer = {};
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));

  Operator inner_nl_join_a_first = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_first = OperatorNode(inner_nl_join_a_first, std::move(children_larger_outer), nullptr);
  auto gexpr_inner_nl_join =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_first));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join, false);
  auto left_group = context_.GetMemo().GetGroupByID(group_id_t(0));
  auto right_group = context_.GetMemo().GetGroupByID(group_id_t(1));
  auto curr_group = context_.GetMemo().GetGroupByID(group_id_t(2));
  left_group->SetNumRows(table_stats_obj_a_.GetNumRows());
  right_group->SetNumRows(table_stats_obj_c_.GetNumRows());
  curr_group->SetNumRows(1000);
  auto left_gexpr = left_group->GetPhysicalExpressions()[0];
  auto right_gexpr = right_group->GetPhysicalExpressions()[0];
  auto left_prop_set = new PropertySet();
  auto right_prop_set = new PropertySet();

  left_group->SetExpressionCost(left_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), left_gexpr),
                                left_prop_set);
  right_group->SetExpressionCost(right_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), right_gexpr),
                                 right_prop_set);
  auto cost_larger_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_smaller_outer = {};
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));

  Operator inner_nl_join_a_second = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_second = OperatorNode(inner_nl_join_a_second, std::move(children_smaller_outer), nullptr);
  auto gexpr_inner_nl_join_2 =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_second));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join_2, false);
  auto curr_group_2 = context_.GetMemo().GetGroupByID(group_id_t(3));
  curr_group_2->SetNumRows(1000);
  auto cost_smaller_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join_2);
  //should be less than
  EXPECT_LT(cost_smaller_outer, cost_larger_outer);
}

TEST_F(CostModelTests, InnerNLJoinCorrectnessTest4) {
  OptimizerContext context_((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_.SetStatsStorage(&stats_storage_);
  // create child gexprs
  auto seq_scan_1 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(3),
                                  std::vector<AnnotatedExpression>(), "table", false);
  auto seq_scan_2 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(2),
                                  std::vector<AnnotatedExpression>(), "table", false);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_larger_outer = {};
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));

  Operator inner_nl_join_a_first = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_first = OperatorNode(inner_nl_join_a_first, std::move(children_larger_outer), nullptr);
  auto gexpr_inner_nl_join =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_first));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join, false);
  auto left_group = context_.GetMemo().GetGroupByID(group_id_t(0));
  auto right_group = context_.GetMemo().GetGroupByID(group_id_t(1));
  auto curr_group = context_.GetMemo().GetGroupByID(group_id_t(2));
  left_group->SetNumRows(table_stats_obj_c_.GetNumRows());
  right_group->SetNumRows(table_stats_obj_b_.GetNumRows());
  curr_group->SetNumRows(1000);
  auto left_gexpr = left_group->GetPhysicalExpressions()[0];
  auto right_gexpr = right_group->GetPhysicalExpressions()[0];
  auto left_prop_set = new PropertySet();
  auto right_prop_set = new PropertySet();

  left_group->SetExpressionCost(left_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), left_gexpr),
                                left_prop_set);
  right_group->SetExpressionCost(right_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), right_gexpr),
                                 right_prop_set);
  auto cost_larger_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_smaller_outer = {};
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));

  Operator inner_nl_join_a_second = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_second = OperatorNode(inner_nl_join_a_second, std::move(children_smaller_outer), nullptr);
  auto gexpr_inner_nl_join_2 =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_second));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join_2, false);
  auto curr_group_2 = context_.GetMemo().GetGroupByID(group_id_t(3));
  curr_group_2->SetNumRows(1000);
  auto cost_smaller_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join_2);
  //should be less than
  EXPECT_LT(cost_smaller_outer, cost_larger_outer);
}

TEST_F(CostModelTests, InnerNLJoinCorrectnessTest5) {
  OptimizerContext context_((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_.SetStatsStorage(&stats_storage_);
  // create child gexprs
  auto seq_scan_1 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(4),
                                  std::vector<AnnotatedExpression>(), "table", false);
  auto seq_scan_2 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(2),
                                  std::vector<AnnotatedExpression>(), "table", false);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_larger_outer = {};
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));

  Operator inner_nl_join_a_first = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_first = OperatorNode(inner_nl_join_a_first, std::move(children_larger_outer), nullptr);
  auto gexpr_inner_nl_join =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_first));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join, false);
  auto left_group = context_.GetMemo().GetGroupByID(group_id_t(0));
  auto right_group = context_.GetMemo().GetGroupByID(group_id_t(1));
  auto curr_group = context_.GetMemo().GetGroupByID(group_id_t(2));
  left_group->SetNumRows(table_stats_obj_d_.GetNumRows());
  right_group->SetNumRows(table_stats_obj_b_.GetNumRows());
  curr_group->SetNumRows(1000);
  auto left_gexpr = left_group->GetPhysicalExpressions()[0];
  auto right_gexpr = right_group->GetPhysicalExpressions()[0];
  auto left_prop_set = new PropertySet();
  auto right_prop_set = new PropertySet();

  left_group->SetExpressionCost(left_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), left_gexpr),
                                left_prop_set);
  right_group->SetExpressionCost(right_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), right_gexpr),
                                 right_prop_set);
  auto cost_larger_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_smaller_outer = {};
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));

  Operator inner_nl_join_a_second = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_second = OperatorNode(inner_nl_join_a_second, std::move(children_smaller_outer), nullptr);
  auto gexpr_inner_nl_join_2 =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_second));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join_2, false);
  auto curr_group_2 = context_.GetMemo().GetGroupByID(group_id_t(3));
  curr_group_2->SetNumRows(1000);
  auto cost_smaller_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join_2);
  //should be less than
  EXPECT_LT(cost_smaller_outer, cost_larger_outer);
}

TEST_F(CostModelTests, InnerNLJoinCorrectnessTest6) {
  OptimizerContext context_((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_.SetStatsStorage(&stats_storage_);
  // create child gexprs
  auto seq_scan_1 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(1),
                                  std::vector<AnnotatedExpression>(), "table", false);
  auto seq_scan_2 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(4),
                                  std::vector<AnnotatedExpression>(), "table", false);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_larger_outer = {};
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));
  children_larger_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));

  Operator inner_nl_join_a_first = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_first = OperatorNode(inner_nl_join_a_first, std::move(children_larger_outer), nullptr);
  auto gexpr_inner_nl_join =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_first));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join, false);
  auto left_group = context_.GetMemo().GetGroupByID(group_id_t(0));
  auto right_group = context_.GetMemo().GetGroupByID(group_id_t(1));
  auto curr_group = context_.GetMemo().GetGroupByID(group_id_t(2));
  left_group->SetNumRows(table_stats_obj_a_.GetNumRows());
  right_group->SetNumRows(table_stats_obj_d_.GetNumRows());
  curr_group->SetNumRows(1000);
  auto left_gexpr = left_group->GetPhysicalExpressions()[0];
  auto right_gexpr = right_group->GetPhysicalExpressions()[0];
  auto left_prop_set = new PropertySet();
  auto right_prop_set = new PropertySet();

  left_group->SetExpressionCost(left_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), left_gexpr),
                                left_prop_set);
  right_group->SetExpressionCost(right_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), right_gexpr),
                                 right_prop_set);
  auto cost_larger_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_smaller_outer = {};
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));

  Operator inner_nl_join_a_second = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression_a_second = OperatorNode(inner_nl_join_a_second, std::move(children_smaller_outer), nullptr);
  auto gexpr_inner_nl_join_2 =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_second));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join_2, false);
  auto curr_group_2 = context_.GetMemo().GetGroupByID(group_id_t(3));
  curr_group_2->SetNumRows(1000);
  auto cost_smaller_outer = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join_2);
  //should be less than
  EXPECT_LT(cost_smaller_outer, cost_larger_outer);
}

TEST_F(CostModelTests, HashJoinCorrectnessTest) {
  OptimizerContext context_((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_.SetStatsStorage(&stats_storage_);
// create child gexprs
  auto seq_scan_1 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(1),
                                  std::vector<AnnotatedExpression>(), "table", false);
  auto seq_scan_2 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(2),
                                  std::vector<AnnotatedExpression>(), "table", false);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_smaller = {};
  children_smaller.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));
  children_smaller.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));

  execution::compiler::test::ExpressionMaker expr_maker;
// Get Table columns
  auto col1 = expr_maker.MakeManaged(std::make_unique<parser::ColumnValueExpression>(
      catalog::db_oid_t(1), catalog::table_oid_t(1), catalog::col_oid_t(1)));
  auto col2 = expr_maker.MakeManaged(std::make_unique<parser::ColumnValueExpression>(
      catalog::db_oid_t(1), catalog::table_oid_t(2), catalog::col_oid_t(1)));

// make first inner hash join
  Operator inner_hash_join_a_first =
      InnerHashJoin::Make(std::vector<AnnotatedExpression>(),
                          std::vector<common::ManagedPointer<parser::AbstractExpression>>{
                              common::ManagedPointer<parser::AbstractExpression>(col1.Get())},
                          std::vector<common::ManagedPointer<parser::AbstractExpression>>{
                              common::ManagedPointer<parser::AbstractExpression>(col2.Get())});
  OperatorNode operator_expression_a_first = OperatorNode(inner_hash_join_a_first, std::move(children_smaller), nullptr);
  auto gexpr_inner_hash_join =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_first));

  context_.GetMemo().InsertExpression(gexpr_inner_hash_join, false);
  auto left_group = context_.GetMemo().GetGroupByID(group_id_t(0));
  auto right_group = context_.GetMemo().GetGroupByID(group_id_t(1));
  auto curr_group = context_.GetMemo().GetGroupByID(group_id_t(2));
  left_group->SetNumRows(table_stats_obj_a_.GetNumRows());
  right_group->SetNumRows(table_stats_obj_b_.GetNumRows());
  curr_group->SetNumRows(1000);

  auto left_gexpr = left_group->GetPhysicalExpressions()[0];
  auto right_gexpr = right_group->GetPhysicalExpressions()[0];
  auto left_prop_set = new PropertySet();
  auto right_prop_set = new PropertySet();

  left_group->SetExpressionCost(left_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), left_gexpr),
                                left_prop_set);
  right_group->SetExpressionCost(right_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), right_gexpr),
                                 right_prop_set);
  auto cost_smaller = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_hash_join);

  OptimizerContext context_2((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_2.SetStatsStorage(&stats_storage_);

  auto seq_scan_5 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(5),
                                  std::vector<AnnotatedExpression>(), "table", false);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_larger = {};
  children_larger.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));
  children_larger.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_5, {}, nullptr)));

  // Get Table columns
  auto col5 = expr_maker.MakeManaged(std::make_unique<parser::ColumnValueExpression>(
      catalog::db_oid_t(1), catalog::table_oid_t(5), catalog::col_oid_t(1)));

  Operator inner_hash_join_a_second =
      InnerHashJoin::Make(std::vector<AnnotatedExpression>(),
                          std::vector<common::ManagedPointer<parser::AbstractExpression>>{
                              common::ManagedPointer<parser::AbstractExpression>(col1.Get())},
                          std::vector<common::ManagedPointer<parser::AbstractExpression>>{
                              common::ManagedPointer<parser::AbstractExpression>(col5.Get())});
  OperatorNode operator_expression_a_second = OperatorNode(inner_hash_join_a_second, std::move(children_larger), nullptr);
  auto gexpr_inner_hash_join_2 =
      context_2.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression_a_second));

  context_2.GetMemo().InsertExpression(gexpr_inner_hash_join_2,false);
  auto left_group_2 = context_2.GetMemo().GetGroupByID(group_id_t(0));
  auto right_group_2 = context_2.GetMemo().GetGroupByID(group_id_t(1));
  auto curr_group_2 = context_2.GetMemo().GetGroupByID(group_id_t(2));
  left_group_2->SetNumRows(table_stats_obj_a_.GetNumRows());
  right_group_2->SetNumRows(table_stats_obj_e_.GetNumRows());
  curr_group_2->SetNumRows(1000);

  auto left_gexpr_2 = left_group_2->GetPhysicalExpressions()[0];
  auto right_gexpr_2 = right_group_2->GetPhysicalExpressions()[0];
  auto left_prop_set_2 = new PropertySet();
  auto right_prop_set_2 = new PropertySet();

  left_group_2->SetExpressionCost(left_gexpr_2, cost_model_.CalculateCost(nullptr, nullptr, &context_2.GetMemo(), left_gexpr_2),
                                left_prop_set_2);
  right_group_2->SetExpressionCost(right_gexpr_2, cost_model_.CalculateCost(nullptr, nullptr, &context_2.GetMemo(), right_gexpr_2),
                                 right_prop_set_2);

  auto cost_larger = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_hash_join_2);

  EXPECT_GT(cost_larger, cost_smaller);
}

TEST_F(CostModelTests, InnerNLJoinVsHashJoinCorrectnessTest) {
  OptimizerContext context_((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_.SetStatsStorage(&stats_storage_);
  // create child gexprs
  auto seq_scan_1 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(1),
                                  std::vector<AnnotatedExpression>(), "table", false);
  auto seq_scan_2 = SeqScan::Make(catalog::db_oid_t(1), catalog::table_oid_t(2),
                                  std::vector<AnnotatedExpression>(), "table", false);

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_smaller_outer = {};
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));
  children_smaller_outer.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));

  //Inner NL join (smaller outer)
  Operator inner_nl_join = InnerNLJoin::Make(std::vector<AnnotatedExpression>());
  OperatorNode operator_expression= OperatorNode(inner_nl_join, std::move(children_smaller_outer), nullptr);
  auto gexpr_inner_nl_join =
      context_.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&operator_expression));
  context_.GetMemo().InsertExpression(gexpr_inner_nl_join, false);
  auto left_group = context_.GetMemo().GetGroupByID(group_id_t(0));
  auto right_group = context_.GetMemo().GetGroupByID(group_id_t(1));
  auto curr_group = context_.GetMemo().GetGroupByID(group_id_t(2));
  left_group->SetNumRows(table_stats_obj_a_.GetNumRows());
  right_group->SetNumRows(table_stats_obj_b_.GetNumRows());
  curr_group->SetNumRows(1000);
  auto left_gexpr = left_group->GetPhysicalExpressions()[0];
  auto right_gexpr = right_group->GetPhysicalExpressions()[0];
  auto left_prop_set = new PropertySet();
  auto right_prop_set = new PropertySet();

  left_group->SetExpressionCost(left_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), left_gexpr),
                                left_prop_set);
  right_group->SetExpressionCost(right_gexpr, cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), right_gexpr),
                                 right_prop_set);

  auto inner_nl_cost = cost_model_.CalculateCost(nullptr, nullptr, &context_.GetMemo(), gexpr_inner_nl_join);

  // Hash join
  OptimizerContext context_2((common::ManagedPointer<AbstractCostModel>(&cost_model_)));
  context_2.SetStatsStorage(&stats_storage_);
  execution::compiler::test::ExpressionMaker expr_maker;
  // Get Table columns
  auto col1 = expr_maker.MakeManaged(std::make_unique<parser::ColumnValueExpression>(
      catalog::db_oid_t(1), catalog::table_oid_t(1), catalog::col_oid_t(1)));
  auto col2 = expr_maker.MakeManaged(std::make_unique<parser::ColumnValueExpression>(
      catalog::db_oid_t(1), catalog::table_oid_t(2), catalog::col_oid_t(1)));

  // make first inner hash join

  Operator inner_hash_join =
      InnerHashJoin::Make(std::vector<AnnotatedExpression>(),
                          std::vector<common::ManagedPointer<parser::AbstractExpression>>{
                              common::ManagedPointer<parser::AbstractExpression>(col1.Get())},
                          std::vector<common::ManagedPointer<parser::AbstractExpression>>{
                              common::ManagedPointer<parser::AbstractExpression>(col2.Get())});

  std::vector<std::unique_ptr<AbstractOptimizerNode>> children_smaller_outer_2 = {};
  children_smaller_outer_2.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_2, {}, nullptr)));
  children_smaller_outer_2.push_back(std::make_unique<OperatorNode>(OperatorNode(seq_scan_1, {}, nullptr)));

  OperatorNode hash_operator_expression = OperatorNode(inner_hash_join, std::move(children_smaller_outer_2), nullptr);
  auto gexpr_inner_hash_join =
      context_2.MakeGroupExpression(common::ManagedPointer<AbstractOptimizerNode>(&hash_operator_expression));

  context_2.GetMemo().InsertExpression(gexpr_inner_hash_join, false);
  auto hash_cost = cost_model_.CalculateCost(nullptr, nullptr, &context_2.GetMemo(), gexpr_inner_hash_join);

  EXPECT_LT(hash_cost, inner_nl_cost);
}
}  // namespace terrier::optimizer