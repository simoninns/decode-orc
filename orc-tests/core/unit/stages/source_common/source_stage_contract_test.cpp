/*
 * File:        source_stage_contract_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Shared source-stage metadata contract tests for source stage families.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>

#include "../../../../orc/core/stages/stage.h"
#include "../../../../orc/plugins/stages/ntsc_comp_source/ntsc_comp_source_stage.h"
#include "../../../../orc/plugins/stages/ntsc_yc_source/ntsc_yc_source_stage.h"
#include "../../../../orc/plugins/stages/pal_comp_source/pal_comp_source_stage.h"
#include "../../../../orc/plugins/stages/pal_yc_source/pal_yc_source_stage.h"

namespace orc_unit_test
{
    namespace
    {
        struct SourceStageContractCase
        {
            const char* case_name;
            std::function<std::unique_ptr<orc::DAGStage>()> create_stage;
            const char* expected_stage_name;
            orc::VideoFormatCompatibility expected_compatibility;
        };

        class SourceStageContractTest : public testing::TestWithParam<SourceStageContractCase>
        {
        };

        TEST_P(SourceStageContractTest, metadataMatchesSourceContract)
        {
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
            SourceStages,
            SourceStageContractTest,
            testing::Values(
                SourceStageContractCase{
                    "NTSCCompSource",
                    []() { return std::make_unique<orc::NTSCCompSourceStage>(); },
                    "NTSC_Comp_Source",
                    orc::VideoFormatCompatibility::NTSC_ONLY},
                SourceStageContractCase{
                    "NTSCYCSource",
                    []() { return std::make_unique<orc::NTSCYCSourceStage>(); },
                    "NTSC_YC_Source",
                    orc::VideoFormatCompatibility::NTSC_ONLY},
                SourceStageContractCase{
                    "PALCompSource",
                    []() { return std::make_unique<orc::PALCompSourceStage>(); },
                    "PAL_Comp_Source",
                    orc::VideoFormatCompatibility::PAL_ONLY},
                SourceStageContractCase{
                    "PALYCSource",
                    []() { return std::make_unique<orc::PALYCSourceStage>(); },
                    "PAL_YC_Source",
                    orc::VideoFormatCompatibility::PAL_ONLY}),
            [](const testing::TestParamInfo<SourceStageContractCase>& info) {
                return info.param.case_name;
            });
    } // namespace
} // namespace orc_unit_test
