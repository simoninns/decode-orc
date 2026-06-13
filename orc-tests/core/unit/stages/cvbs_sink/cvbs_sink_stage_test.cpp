/*
 * File:        cvbs_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for CVBSSinkStage parameter contracts and trigger
 * dependency seam
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/cvbs_sink/cvbs_sink_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>

#include "../../../../orc/plugins/stages/cvbs_sink/cvbs_sink_stage_deps_interface.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_frame_representation_artifact_mock.h"

namespace orc_unit_test {
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

class MockCVBSSinkStageDeps : public orc::ICVBSSinkStageDeps {
 public:
  MOCK_METHOD(void, init,
              (orc::TriggerProgressCallback progress_callback,
               std::atomic<bool>* cancel_requested),
              (override));

  MOCK_METHOD(orc::CVBSSinkWriteResult, write_cvbs,
              (const orc::VideoFrameRepresentation* representation,
               const std::string& output_path),
              (override));
};

TEST(CVBSSinkStageTest, StageInterfaceInvariants_MatchSink) {
  orc::CVBSSinkStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
  EXPECT_EQ(stage.output_count(), 0u);
  EXPECT_EQ(stage.get_node_type_info().type, orc::NodeType::SINK);
}

TEST(CVBSSinkStageTest, StageName_IsCVBSSink) {
  orc::CVBSSinkStage stage;
  EXPECT_EQ(stage.get_node_type_info().stage_name, "CVBSSink");
}

TEST(CVBSSinkStageTest, Descriptor_OutputPathIsCvbsFile) {
  orc::CVBSSinkStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  auto it = std::find_if(descriptors.begin(), descriptors.end(),
                         [](const orc::ParameterDescriptor& d) {
                           return d.name == "output_path";
                         });

  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->type, orc::ParameterType::FILE_PATH);
  EXPECT_EQ(it->file_extension_hint, ".cvbs");
  if (!it->constraints.default_value.has_value()) {
    FAIL() << "Expected default_value to have a value";
    return;
  }
  EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), "");
}

TEST(CVBSSinkStageTest, Trigger_FailsWhenNoInputProvided) {
  orc::CVBSSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger({}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_THAT(stage.get_trigger_status(),
              testing::HasSubstr("requires one input"));
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(CVBSSinkStageTest, Trigger_FailsWhenInputIsNotVideoFrameRepresentation) {
  orc::CVBSSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger(
      {nullptr}, {{"output_path", std::string("out.cvbs")}},
      observation_context);

  EXPECT_FALSE(result);
  EXPECT_THAT(stage.get_trigger_status(),
              testing::HasSubstr("VideoFrameRepresentation"));
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(CVBSSinkStageTest, Trigger_FailsWhenOutputPathMissing) {
  orc::CVBSSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_THAT(stage.get_trigger_status(),
              testing::HasSubstr("output_path parameter is required"));
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(CVBSSinkStageTest, Trigger_UsesDepsSeamAndReportsSuccess) {
  orc::CVBSSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockCVBSSinkStageDeps>>();
  stage.set_deps_override(deps);

  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*deps, write_cvbs(vfr.get(), "out.cvbs"))
      .WillOnce(Return(orc::CVBSSinkWriteResult{
          true, 42, "Written 42 frames to out.cvbs"}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.cvbs")}}, observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Written 42 frames to out.cvbs");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(CVBSSinkStageTest, Trigger_UsesDepsSeamAndPropagatesFailure) {
  orc::CVBSSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockCVBSSinkStageDeps>>();
  stage.set_deps_override(deps);

  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*deps, write_cvbs(vfr.get(), "out.cvbs"))
      .WillOnce(Return(orc::CVBSSinkWriteResult{false, 0, "Cancelled by user"}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.cvbs")}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Cancelled by user");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}
}  // namespace orc_unit_test
