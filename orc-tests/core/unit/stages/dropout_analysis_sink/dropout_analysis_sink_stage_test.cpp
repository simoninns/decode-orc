/*
 * File:        dropout_analysis_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for DropoutAnalysisSinkStage contracts and trigger
 * behavior
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/dropout_analysis_sink/dropout_analysis_sink_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/plugins/stages/dropout_analysis_sink/dropout_analysis_sink_deps_interface.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_field_representation_mock.h"

namespace orc_unit_test {
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

class MockDropoutAnalysisSinkStageDeps
    : public orc::IDropoutAnalysisSinkStageDeps {
 public:
  MOCK_METHOD(void, init,
              (orc::TriggerProgressCallback progress_callback,
               std::atomic<bool>* cancel_requested),
              (override));

  MOCK_METHOD(orc::DropoutAnalysisComputeResult, compute_and_analyze,
              (orc::VideoFieldRepresentation * representation,
               orc::IObservationContext& observation_context,
               orc::DropoutAnalysisComputeOptions options),
              (override));

  MOCK_METHOD(bool, write_csv,
              (const std::string& path,
               const std::vector<orc::FrameDropoutStats>& frame_stats),
              (override));
};

TEST(DropoutAnalysisSinkStageTest,
     descriptorDefaultsIncludeExpectedOutputAndWriteCsv) {
  orc::DropoutAnalysisSinkStage stage;
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

TEST(DropoutAnalysisSinkStageTest, triggerFailsWhenNoInputProvided) {
  orc::DropoutAnalysisSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger({}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Error: No input connected");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(DropoutAnalysisSinkStageTest, triggerSucceedsWhenInputRangeIsEmpty) {
  orc::DropoutAnalysisSinkStage stage;
  orc::ObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*vfr, field_range())
      .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(0))));

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Dropout analysis complete");
  EXPECT_TRUE(stage.has_results());
  EXPECT_EQ(stage.total_frames(), 0);
  EXPECT_TRUE(stage.frame_stats().empty());
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(DropoutAnalysisSinkStageTest, triggerUsesDepsSeamAndReportsSuccess) {
  orc::DropoutAnalysisSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockDropoutAnalysisSinkStageDeps>>();
  stage.set_deps_override(deps);

  orc::ObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  std::vector<orc::FrameDropoutStats> expected_stats;
  orc::FrameDropoutStats stat{};
  stat.frame_number = 12;
  stat.total_dropout_length = 128.0;
  stat.dropout_count = 3.0;
  stat.has_data = true;
  expected_stats.push_back(stat);

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
      .WillOnce(Return(orc::DropoutAnalysisComputeResult{
          true, "Dropout analysis complete", expected_stats, 240}));
  EXPECT_CALL(*deps, write_csv(_, _)).Times(0);

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Dropout analysis complete");
  EXPECT_TRUE(stage.has_results());
  ASSERT_EQ(stage.frame_stats().size(), 1u);
  EXPECT_EQ(stage.frame_stats()[0].frame_number, 12);
  EXPECT_DOUBLE_EQ(stage.frame_stats()[0].total_dropout_length, 128.0);
  EXPECT_DOUBLE_EQ(stage.frame_stats()[0].dropout_count, 3.0);
  EXPECT_EQ(stage.total_frames(), 240);
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(DropoutAnalysisSinkStageTest, triggerUsesDepsSeamAndPropagatesFailure) {
  orc::DropoutAnalysisSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockDropoutAnalysisSinkStageDeps>>();
  stage.set_deps_override(deps);

  orc::ObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
      .WillOnce(Return(
          orc::DropoutAnalysisComputeResult{false, "observer failed", {}, 0}));
  EXPECT_CALL(*deps, write_csv(_, _)).Times(0);

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Error: observer failed");
  EXPECT_FALSE(stage.has_results());
  EXPECT_TRUE(stage.frame_stats().empty());
  EXPECT_EQ(stage.total_frames(), 0);
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(DropoutAnalysisSinkStageTest, triggerWritesCSVWhenDepsSucceeds) {
  orc::DropoutAnalysisSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockDropoutAnalysisSinkStageDeps>>();
  stage.set_deps_override(deps);

  orc::ObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  std::vector<orc::FrameDropoutStats> expected_stats;
  orc::FrameDropoutStats stat{};
  stat.frame_number = 4;
  stat.total_dropout_length = 64.0;
  stat.dropout_count = 2.0;
  stat.has_data = true;
  expected_stats.push_back(stat);

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps, compute_and_analyze(vfr.get(), _, _))
      .WillOnce(Return(orc::DropoutAnalysisComputeResult{
          true, "Dropout analysis complete", expected_stats, 8}));
  EXPECT_CALL(*deps, write_csv("out.csv", _))
      .WillOnce(testing::Invoke(
          [](const std::string& path,
             const std::vector<orc::FrameDropoutStats>& frame_stats) {
            EXPECT_EQ(path, "out.csv");
            EXPECT_EQ(frame_stats.size(), 1u);
            EXPECT_EQ(frame_stats[0].frame_number, 4);
            EXPECT_DOUBLE_EQ(frame_stats[0].total_dropout_length, 64.0);
            EXPECT_DOUBLE_EQ(frame_stats[0].dropout_count, 2.0);
            return true;
          }));

  const bool result = stage.trigger(
      {vfr}, {{"write_csv", true}, {"output_path", std::string("out.csv")}},
      observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Dropout analysis complete");
  EXPECT_TRUE(stage.has_results());
  EXPECT_EQ(stage.total_frames(), 8);
  EXPECT_FALSE(stage.is_trigger_in_progress());
}
}  // namespace orc_unit_test
