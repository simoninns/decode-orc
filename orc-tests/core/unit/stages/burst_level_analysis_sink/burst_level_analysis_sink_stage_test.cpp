/*
 * File:        burst_level_analysis_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for BurstLevelAnalysisSinkStage contracts and trigger
 * behavior
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/burst_level_analysis_sink/burst_level_analysis_sink_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/plugins/stages/burst_level_analysis_sink/burst_level_analysis_sink_deps_interface.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_frame_representation_artifact_mock.h"

namespace orc_unit_test {
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

class MockBurstLevelAnalysisSinkStageDeps
    : public orc::IBurstLevelAnalysisSinkStageDeps {
 public:
  MOCK_METHOD(void, init,
              (orc::TriggerProgressCallback progress_callback,
               std::atomic<bool>* cancel_requested),
              (override));

  MOCK_METHOD(orc::BurstAnalysisComputeResult, compute_and_analyze,
              (orc::VideoFrameRepresentation * representation,
               orc::IObservationContext& observation_context,
               orc::BurstAnalysisComputeOptions options),
              (override));

  MOCK_METHOD(bool, write_csv,
              (const std::string& path,
               const std::vector<orc::FrameBurstLevelStats>& frame_stats),
              (override));
};

TEST(BurstLevelAnalysisSinkStageTest,
     Descriptor_DefaultsIncludeExpectedOutputAndWriteCsv) {
  orc::BurstLevelAnalysisSinkStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  auto output_it = std::find_if(descriptors.begin(), descriptors.end(),
                                [](const orc::ParameterDescriptor& d) {
                                  return d.name == "output_path";
                                });
  auto write_csv_it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [](const orc::ParameterDescriptor& d) { return d.name == "write_csv"; });

  ASSERT_NE(output_it, descriptors.end());
  EXPECT_EQ(output_it->type, orc::ParameterType::FILE_PATH);
  if (!output_it->constraints.default_value.has_value() ||
      !write_csv_it->constraints.default_value.has_value()) {
    FAIL() << "Expected all descriptors to have default values";
    return;
  }
  EXPECT_EQ(std::get<std::string>(*output_it->constraints.default_value), "");

  ASSERT_NE(write_csv_it, descriptors.end());
  EXPECT_EQ(write_csv_it->type, orc::ParameterType::BOOL);
  EXPECT_FALSE(std::get<bool>(*write_csv_it->constraints.default_value));
}

TEST(BurstLevelAnalysisSinkStageTest, Trigger_FailsWhenNoInputProvided) {
  orc::BurstLevelAnalysisSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger({}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Error: No input connected");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(BurstLevelAnalysisSinkStageTest, Trigger_SucceedsWhenInputRangeIsEmpty) {
  orc::BurstLevelAnalysisSinkStage stage;
  orc::ObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  // Empty range: last < first → count() == 0
  EXPECT_CALL(*vfr, frame_range()).WillOnce(Return(orc::FrameIDRange{1, 0}));

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Burst level analysis complete");
  EXPECT_TRUE(stage.has_results());
  EXPECT_EQ(stage.total_frames(), 0);
  EXPECT_TRUE(stage.frame_stats().empty());
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(BurstLevelAnalysisSinkStageTest, Trigger_UsesDepsSeamAndReportsSuccess) {
  orc::BurstLevelAnalysisSinkStage stage;
  auto deps =
      std::make_shared<StrictMock<MockBurstLevelAnalysisSinkStageDeps>>();
  stage.set_deps_override(deps);

  orc::ObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  std::vector<orc::FrameBurstLevelStats> expected_stats;
  orc::FrameBurstLevelStats stat{};
  stat.frame_number = 12;
  stat.median_burst_10bit = 8.5;
  stat.field_count = 2;
  stat.has_data = true;
  expected_stats.push_back(stat);

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
      .WillOnce(Return(orc::BurstAnalysisComputeResult{
          true, "Burst level analysis complete", expected_stats, 240}));
  EXPECT_CALL(*deps, write_csv(_, _)).Times(0);

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Burst level analysis complete");
  EXPECT_TRUE(stage.has_results());
  ASSERT_EQ(stage.frame_stats().size(), 1u);
  EXPECT_EQ(stage.frame_stats()[0].frame_number, 12);
  EXPECT_DOUBLE_EQ(stage.frame_stats()[0].median_burst_10bit, 8.5);
  EXPECT_EQ(stage.total_frames(), 240);
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(BurstLevelAnalysisSinkStageTest,
     Trigger_UsesDepsSeamAndPropagatesFailure) {
  orc::BurstLevelAnalysisSinkStage stage;
  auto deps =
      std::make_shared<StrictMock<MockBurstLevelAnalysisSinkStageDeps>>();
  stage.set_deps_override(deps);

  orc::ObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
      .WillOnce(Return(
          orc::BurstAnalysisComputeResult{false, "observer failed", {}, 0}));
  EXPECT_CALL(*deps, write_csv(_, _)).Times(0);

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Error: observer failed");
  EXPECT_FALSE(stage.has_results());
  EXPECT_TRUE(stage.frame_stats().empty());
  EXPECT_EQ(stage.total_frames(), 0);
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(BurstLevelAnalysisSinkStageTest, Trigger_WritesCSVWhenDepsSucceeds) {
  orc::BurstLevelAnalysisSinkStage stage;
  auto deps =
      std::make_shared<StrictMock<MockBurstLevelAnalysisSinkStageDeps>>();
  stage.set_deps_override(deps);

  orc::ObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  std::vector<orc::FrameBurstLevelStats> expected_stats;
  orc::FrameBurstLevelStats stat{};
  stat.frame_number = 4;
  stat.median_burst_10bit = 10.25;
  stat.field_count = 2;
  stat.has_data = true;
  expected_stats.push_back(stat);

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
      .WillOnce(Return(orc::BurstAnalysisComputeResult{
          true, "Burst level analysis complete", expected_stats, 8}));
  EXPECT_CALL(*deps, write_csv("out.csv", _))
      .WillOnce(testing::Invoke(
          [](const std::string& path,
             const std::vector<orc::FrameBurstLevelStats>& frame_stats) {
            EXPECT_EQ(path, "out.csv");
            EXPECT_EQ(frame_stats.size(), 1u);
            EXPECT_EQ(frame_stats[0].frame_number, 4);
            EXPECT_DOUBLE_EQ(frame_stats[0].median_burst_10bit, 10.25);
            return true;
          }));

  const bool result = stage.trigger(
      {vfr}, {{"write_csv", true}, {"output_path", std::string("out.csv")}},
      observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Burst level analysis complete");
  EXPECT_TRUE(stage.has_results());
  EXPECT_EQ(stage.total_frames(), 8);
  EXPECT_FALSE(stage.is_trigger_in_progress());
}
}  // namespace orc_unit_test
