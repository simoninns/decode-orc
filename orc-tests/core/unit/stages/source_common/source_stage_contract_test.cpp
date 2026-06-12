/*
 * File:        source_stage_contract_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Shared source-stage metadata contract tests for source stage
 * families.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>

#include "../../../../orc/core/stages/stage.h"
#include "../../../../orc/plugins/stages/tbc_source/tbc_source_stage.h"

namespace orc_unit_test {
namespace {
struct SourceStageContractCase {
  const char* case_name;
  std::function<std::unique_ptr<orc::DAGStage>()> create_stage;
  const char* expected_stage_name;
  orc::VideoFormatCompatibility expected_compatibility;
};

class SourceStageContractTest
    : public testing::TestWithParam<SourceStageContractCase> {};

TEST_P(SourceStageContractTest, Metadata_MatchesSourceContract) {
  const auto& test_case = GetParam();
  auto stage = test_case.create_stage();
  const auto info = stage->get_node_type_info();

  EXPECT_EQ(stage->required_input_count(), 0u);
  EXPECT_EQ(stage->output_count(), 1u);
  EXPECT_EQ(info.type, orc::NodeType::SOURCE);
  EXPECT_EQ(info.stage_name, test_case.expected_stage_name);
  EXPECT_EQ(info.compatible_formats, test_case.expected_compatibility);
}

INSTANTIATE_TEST_SUITE_P(
    SourceStages, SourceStageContractTest,
    testing::Values(
        SourceStageContractCase{
            "TBCSource",
            []() { return std::make_unique<orc::TBCSourceStage>(); },
            "tbc_source", orc::VideoFormatCompatibility::ALL}),
    [](const testing::TestParamInfo<SourceStageContractCase>& info) {
      return info.param.case_name;
    });
}  // namespace
}  // namespace orc_unit_test
