/*
 * File:        burst_level_analysis_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for BurstLevelAnalysisSinkStage contracts and trigger behavior
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "../../include/video_field_representation_mock.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/plugins/stages/burst_level_analysis_sink/burst_level_analysis_sink_stage.h"
#include "../../../../orc/plugins/stages/burst_level_analysis_sink/burst_level_analysis_sink_deps_interface.h"

namespace orc_unit_test
{
    using testing::NiceMock;
    using testing::StrictMock;
    using testing::Return;
    using testing::_;

    class MockBurstLevelAnalysisSinkStageDeps : public orc::IBurstLevelAnalysisSinkStageDeps
    {
    public:
        MOCK_METHOD(
            void,
            init,
            (orc::TriggerProgressCallback progress_callback, std::atomic<bool>* cancel_requested),
            (override));

        MOCK_METHOD(
            orc::BurstAnalysisComputeResult,
            compute_and_analyze,
            (orc::VideoFieldRepresentation * representation,
             orc::IObservationContext & observation_context,
             orc::BurstAnalysisComputeOptions options),
            (override));

        MOCK_METHOD(
            bool,
            write_csv,
            (const std::string& path, const std::vector<orc::FrameBurstLevelStats>& frame_stats),
            (override));
    };

    TEST(BurstLevelAnalysisSinkStageTest, descriptorDefaults_includeExpectedOutputAndWriteCsv)
    {
        orc::BurstLevelAnalysisSinkStage stage;
        const auto descriptors = stage.get_parameter_descriptors();

        auto output_it = std::find_if(descriptors.begin(), descriptors.end(), [](const orc::ParameterDescriptor& d) {
            return d.name == "output_path";
        });
        auto write_csv_it = std::find_if(descriptors.begin(), descriptors.end(), [](const orc::ParameterDescriptor& d) {
            return d.name == "write_csv";
        });

        ASSERT_NE(output_it, descriptors.end());
        EXPECT_EQ(output_it->type, orc::ParameterType::FILE_PATH);
        EXPECT_EQ(std::get<std::string>(*output_it->constraints.default_value), "");

        ASSERT_NE(write_csv_it, descriptors.end());
        EXPECT_EQ(write_csv_it->type, orc::ParameterType::BOOL);
        EXPECT_FALSE(std::get<bool>(*write_csv_it->constraints.default_value));
    }

    TEST(BurstLevelAnalysisSinkStageTest, triggerFails_whenNoInputProvided)
    {
        orc::BurstLevelAnalysisSinkStage stage;
        MockObservationContext observation_context;

        const bool result = stage.trigger({}, {}, observation_context);

        EXPECT_FALSE(result);
        EXPECT_EQ(stage.get_trigger_status(), "Error: No input connected");
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(BurstLevelAnalysisSinkStageTest, triggerSucceeds_whenInputRangeIsEmpty)
    {
        orc::BurstLevelAnalysisSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*vfr, field_range()).WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

        const bool result = stage.trigger({vfr}, {}, observation_context);

        EXPECT_TRUE(result);
        EXPECT_EQ(stage.get_trigger_status(), "Burst level analysis complete");
        EXPECT_TRUE(stage.has_results());
        EXPECT_EQ(stage.total_frames(), 0);
        EXPECT_TRUE(stage.frame_stats().empty());
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(BurstLevelAnalysisSinkStageTest, triggerUsesDepsSeam_andReportsSuccess)
    {
        orc::BurstLevelAnalysisSinkStage stage;
        auto deps = std::make_shared<StrictMock<MockBurstLevelAnalysisSinkStageDeps>>();
        stage.set_deps_override(deps);

        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        std::vector<orc::FrameBurstLevelStats> expected_stats;
        orc::FrameBurstLevelStats stat{};
        stat.frame_number = 12;
        stat.median_burst_ire = 8.5;
        stat.field_count = 2;
        stat.has_data = true;
        expected_stats.push_back(stat);

        EXPECT_CALL(*deps, init(_, _));
        EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
            .WillOnce(Return(orc::BurstAnalysisComputeResult{
                true,
                "Burst level analysis complete",
                expected_stats,
                240
            }));
        EXPECT_CALL(*deps, write_csv(_, _)).Times(0);

        const bool result = stage.trigger({vfr}, {}, observation_context);

        EXPECT_TRUE(result);
        EXPECT_EQ(stage.get_trigger_status(), "Burst level analysis complete");
        EXPECT_TRUE(stage.has_results());
        ASSERT_EQ(stage.frame_stats().size(), 1u);
        EXPECT_EQ(stage.frame_stats()[0].frame_number, 12);
        EXPECT_DOUBLE_EQ(stage.frame_stats()[0].median_burst_ire, 8.5);
        EXPECT_EQ(stage.total_frames(), 240);
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(BurstLevelAnalysisSinkStageTest, triggerUsesDepsSeam_andPropagatesFailure)
    {
        orc::BurstLevelAnalysisSinkStage stage;
        auto deps = std::make_shared<StrictMock<MockBurstLevelAnalysisSinkStageDeps>>();
        stage.set_deps_override(deps);

        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*deps, init(_, _));
        EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
            .WillOnce(Return(orc::BurstAnalysisComputeResult{
                false,
                "observer failed",
                {},
                0
            }));
        EXPECT_CALL(*deps, write_csv(_, _)).Times(0);

        const bool result = stage.trigger({vfr}, {}, observation_context);

        EXPECT_FALSE(result);
        EXPECT_EQ(stage.get_trigger_status(), "Error: observer failed");
        EXPECT_FALSE(stage.has_results());
        EXPECT_TRUE(stage.frame_stats().empty());
        EXPECT_EQ(stage.total_frames(), 0);
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(BurstLevelAnalysisSinkStageTest, triggerWritesCSV_whenDepsSucceeds)
    {
        orc::BurstLevelAnalysisSinkStage stage;
        auto deps = std::make_shared<StrictMock<MockBurstLevelAnalysisSinkStageDeps>>();
        stage.set_deps_override(deps);

        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        std::vector<orc::FrameBurstLevelStats> expected_stats;
        orc::FrameBurstLevelStats stat{};
        stat.frame_number = 4;
        stat.median_burst_ire = 10.25;
        stat.field_count = 2;
        stat.has_data = true;
        expected_stats.push_back(stat);

        EXPECT_CALL(*deps, init(_, _));
        EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
            .WillOnce(Return(orc::BurstAnalysisComputeResult{
                true,
                "Burst level analysis complete",
                expected_stats,
                8
            }));
        EXPECT_CALL(*deps, write_csv("out.csv", _))
            .WillOnce(testing::Invoke([](const std::string& path, const std::vector<orc::FrameBurstLevelStats>& frame_stats) {
                EXPECT_EQ(path, "out.csv");
                EXPECT_EQ(frame_stats.size(), 1u);
                EXPECT_EQ(frame_stats[0].frame_number, 4);
                EXPECT_DOUBLE_EQ(frame_stats[0].median_burst_ire, 10.25);
                return true;
            }));

        const bool result = stage.trigger(
            {vfr},
            {
                {"write_csv", true},
                {"output_path", std::string("out.csv")}
            },
            observation_context);

        EXPECT_TRUE(result);
        EXPECT_EQ(stage.get_trigger_status(), "Burst level analysis complete");
        EXPECT_TRUE(stage.has_results());
        EXPECT_EQ(stage.total_frames(), 8);
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }
}
