/*
 * File:        snr_analysis_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for SNRAnalysisSinkStage contracts and trigger behavior
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
#include "../../../../orc/plugins/stages/snr_analysis_sink/snr_analysis_sink_stage.h"
#include "../../../../orc/plugins/stages/snr_analysis_sink/snr_analysis_sink_deps_interface.h"

namespace orc_unit_test
{
    using testing::NiceMock;
    using testing::StrictMock;
    using testing::Return;
    using testing::_;

    class MockSNRAnalysisSinkStageDeps : public orc::ISNRAnalysisSinkStageDeps
    {
    public:
        MOCK_METHOD(
            void,
            init,
            (orc::TriggerProgressCallback progress_callback, std::atomic<bool>* cancel_requested),
            (override));

        MOCK_METHOD(
            orc::SNRAnalysisComputeResult,
            compute_and_analyze,
            (orc::VideoFieldRepresentation * representation,
             orc::IObservationContext & observation_context,
             orc::SNRAnalysisComputeOptions options),
            (override));

        MOCK_METHOD(
            bool,
            write_csv,
            (const std::string& path, const std::vector<orc::FrameSNRStats>& frame_stats),
            (override));
    };

    TEST(SNRAnalysisSinkStageTest, descriptorDefaults_includeExpectedModes)
    {
        orc::SNRAnalysisSinkStage stage;
        const auto descriptors = stage.get_parameter_descriptors();

        auto mode_it = std::find_if(descriptors.begin(), descriptors.end(), [](const orc::ParameterDescriptor& d) {
            return d.name == "mode";
        });

        ASSERT_NE(mode_it, descriptors.end());
        EXPECT_EQ(mode_it->type, orc::ParameterType::STRING);
        EXPECT_EQ(std::get<std::string>(*mode_it->constraints.default_value), "both");
        EXPECT_EQ(mode_it->constraints.allowed_strings.size(), 3u);
    }

    TEST(SNRAnalysisSinkStageTest, triggerFails_whenNoInputProvided)
    {
        orc::SNRAnalysisSinkStage stage;
        MockObservationContext observation_context;

        const bool result = stage.trigger({}, {}, observation_context);

        EXPECT_FALSE(result);
        EXPECT_EQ(stage.get_trigger_status(), "Error: No input connected");
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(SNRAnalysisSinkStageTest, triggerSucceeds_whenInputRangeIsEmpty)
    {
        orc::SNRAnalysisSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*vfr, field_range()).WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

        const bool result = stage.trigger({vfr}, {{"mode", std::string("white")}}, observation_context);

        EXPECT_TRUE(result);
        EXPECT_EQ(stage.get_trigger_status(), "SNR analysis complete");
        EXPECT_TRUE(stage.has_results());
        EXPECT_EQ(stage.last_mode(), orc::SNRAnalysisMode::WHITE);
        EXPECT_EQ(stage.total_frames(), 0);
        EXPECT_TRUE(stage.frame_stats().empty());
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(SNRAnalysisSinkStageTest, triggerUsesDepsSeam_andReportsSuccess)
    {
        orc::SNRAnalysisSinkStage stage;
        auto deps = std::make_shared<StrictMock<MockSNRAnalysisSinkStageDeps>>();
        stage.set_deps_override(deps);

        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        std::vector<orc::FrameSNRStats> expected_stats;
        orc::FrameSNRStats stat{};
        stat.frame_number = 10;
        stat.white_snr = 24.5;
        stat.black_psnr = 33.1;
        stat.white_snr_count = 2;
        stat.black_psnr_count = 2;
        stat.has_white_snr = true;
        stat.has_black_psnr = true;
        stat.has_data = true;
        expected_stats.push_back(stat);

        orc::SNRAnalysisComputeOptions captured_options;

        EXPECT_CALL(*deps, init(_, _));
        EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
            .WillOnce(testing::DoAll(
                testing::SaveArg<2>(&captured_options),
                Return(orc::SNRAnalysisComputeResult{
                    true,
                    "SNR analysis complete",
                    expected_stats,
                    240
                })));
        EXPECT_CALL(*deps, write_csv(_, _)).Times(0);

        const bool result = stage.trigger(
            {vfr},
            {{"mode", std::string("white")}},
            observation_context);

        EXPECT_TRUE(result);
        EXPECT_EQ(stage.get_trigger_status(), "SNR analysis complete");
        EXPECT_TRUE(stage.has_results());
        ASSERT_EQ(stage.frame_stats().size(), 1u);
        EXPECT_EQ(stage.frame_stats()[0].frame_number, 10);
        EXPECT_DOUBLE_EQ(stage.frame_stats()[0].white_snr, 24.5);
        EXPECT_DOUBLE_EQ(stage.frame_stats()[0].black_psnr, 33.1);
        EXPECT_EQ(stage.total_frames(), 240);
        EXPECT_EQ(stage.last_mode(), orc::SNRAnalysisMode::WHITE);
        EXPECT_EQ(captured_options.snr_mode, orc::SNRAnalysisMode::WHITE);
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(SNRAnalysisSinkStageTest, triggerUsesDepsSeam_andPropagatesFailure)
    {
        orc::SNRAnalysisSinkStage stage;
        auto deps = std::make_shared<StrictMock<MockSNRAnalysisSinkStageDeps>>();
        stage.set_deps_override(deps);

        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*deps, init(_, _));
        EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
            .WillOnce(Return(orc::SNRAnalysisComputeResult{
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

    TEST(SNRAnalysisSinkStageTest, triggerWritesCSV_whenDepsSucceeds)
    {
        orc::SNRAnalysisSinkStage stage;
        auto deps = std::make_shared<StrictMock<MockSNRAnalysisSinkStageDeps>>();
        stage.set_deps_override(deps);

        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        std::vector<orc::FrameSNRStats> expected_stats;
        orc::FrameSNRStats stat{};
        stat.frame_number = 4;
        stat.white_snr = 22.0;
        stat.black_psnr = 31.0;
        stat.white_snr_count = 2;
        stat.black_psnr_count = 2;
        stat.has_white_snr = true;
        stat.has_black_psnr = true;
        stat.has_data = true;
        expected_stats.push_back(stat);

        EXPECT_CALL(*deps, init(_, _));
        EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
            .WillOnce(Return(orc::SNRAnalysisComputeResult{
                true,
                "SNR analysis complete",
                expected_stats,
                8
            }));
        EXPECT_CALL(*deps, write_csv("out.csv", _))
            .WillOnce(testing::Invoke([](const std::string& path, const std::vector<orc::FrameSNRStats>& frame_stats) {
                EXPECT_EQ(path, "out.csv");
                EXPECT_EQ(frame_stats.size(), 1u);
                EXPECT_EQ(frame_stats[0].frame_number, 4);
                EXPECT_DOUBLE_EQ(frame_stats[0].white_snr, 22.0);
                EXPECT_DOUBLE_EQ(frame_stats[0].black_psnr, 31.0);
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
        EXPECT_EQ(stage.get_trigger_status(), "SNR analysis complete");
        EXPECT_TRUE(stage.has_results());
        EXPECT_EQ(stage.total_frames(), 8);
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }
}
