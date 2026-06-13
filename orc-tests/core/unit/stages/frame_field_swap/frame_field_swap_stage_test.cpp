/*
 * File:        frame_field_swap_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for FrameFieldSwapStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/frame_field_swap/frame_field_swap_stage.h"

#include <gtest/gtest.h>

#include "../../../../orc/core/include/observation_context.h"
#include "../../mocks/mock_video_frame_representation.h"

using ::testing::Return;
using ::testing::_;

namespace orc_unit_test {

// ── NodeTypeInfo / interface contracts ─────────────────────────────────────

TEST(FrameFieldSwapStageTest, RequiredInputCount_IsOne) {
  orc::FrameFieldSwapStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(FrameFieldSwapStageTest, OutputCount_IsOne) {
  orc::FrameFieldSwapStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(FrameFieldSwapStageTest, NodeTypeInfo_HasExpectedMetadata) {
  orc::FrameFieldSwapStage stage;
  auto info = stage.get_node_type_info();
  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "frame_field_swap");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(FrameFieldSwapStageTest, ParameterDescriptors_AreEmpty) {
  orc::FrameFieldSwapStage stage;
  EXPECT_TRUE(stage.get_parameter_descriptors().empty());
}

TEST(FrameFieldSwapStageTest, GetParameters_ReturnsEmptyMap) {
  orc::FrameFieldSwapStage stage;
  EXPECT_TRUE(stage.get_parameters().empty());
}

TEST(FrameFieldSwapStageTest, SetParameters_AlwaysSucceeds) {
  orc::FrameFieldSwapStage stage;
  EXPECT_TRUE(stage.set_parameters({{"ignored", int32_t(0)}}));
}

// ── process() ──────────────────────────────────────────────────────────────

TEST(FrameFieldSwapStageTest, Process_ReturnsNullWhenSourceNull) {
  orc::FrameFieldSwapStage stage;
  EXPECT_EQ(stage.process(nullptr), nullptr);
}

TEST(FrameFieldSwapStageTest, Process_WrapsSourceWhenProvided) {
  orc::FrameFieldSwapStage stage;
  auto mock = std::make_shared<MockVideoFrameRepresentation>();
  EXPECT_CALL(*mock, get_video_parameters())
      .WillRepeatedly(Return(std::nullopt));

  auto result = stage.process(mock);
  ASSERT_NE(result, nullptr);
  EXPECT_NE(result.get(), mock.get());
}

// ── execute() ──────────────────────────────────────────────────────────────

TEST(FrameFieldSwapStageTest, Execute_ThrowsWhenInputMissing) {
  orc::FrameFieldSwapStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({}, {}, ctx), orc::DAGExecutionError);
}

TEST(FrameFieldSwapStageTest, Execute_ThrowsWhenInputIsNotVFrameR) {
  orc::FrameFieldSwapStage stage;
  orc::ObservationContext ctx;
  // A plain Artifact that is NOT a VideoFrameRepresentation.
  struct DummyArtifact : public orc::Artifact {
    DummyArtifact() : orc::Artifact(orc::ArtifactID("dummy"), orc::Provenance{}) {}
    std::string type_name() const override { return "dummy"; }
  };
  orc::ArtifactPtr bad_input = std::make_shared<DummyArtifact>();
  EXPECT_THROW(stage.execute({bad_input}, {}, ctx), orc::DAGExecutionError);
}

// ── FrameFieldSwapRepresentation line remapping ────────────────────────────

TEST(FrameFieldSwapRepresentationTest, GetLine_RemapsFieldBlocks_PAL) {
  // PAL: field1=313 lines, field2=312 lines, frame height=625
  // Output line 0 (first line of field2-first output) should map to source
  // line 313 (first line of source field2).
  auto mock = std::make_shared<MockVideoFrameRepresentation>();
  orc::SourceParameters params;
  params.system = orc::VideoSystem::PAL;
  params.frame_width_nominal = 1135;
  params.frame_height = 625;
  params.blanking_level = 256;
  params.white_level = 844;

  orc::FrameDescriptor desc;
  desc.height = 625;
  desc.samples_total = 625 * 1135;

  EXPECT_CALL(*mock, get_video_parameters())
      .WillRepeatedly(Return(params));
  EXPECT_CALL(*mock, get_frame_descriptor(orc::FrameID{0}))
      .WillRepeatedly(Return(desc));

  static const orc::VideoFrameRepresentation::sample_type kLine313[1135] = {};
  // Output line 0 should request source line 313 (field2 first line).
  EXPECT_CALL(*mock, get_line(orc::FrameID{0}, 313))
      .WillOnce(Return(kLine313));

  orc::FrameFieldSwapRepresentation rep(mock);
  const auto* ptr = rep.get_line(orc::FrameID{0}, 0);
  EXPECT_EQ(ptr, kLine313);
}

TEST(FrameFieldSwapRepresentationTest, GetLine_SecondBlock_MapsToField1) {
  // Output line 312 (first line of the second output block) should map to
  // source line 0 (first line of source field1).
  auto mock = std::make_shared<MockVideoFrameRepresentation>();
  orc::SourceParameters params;
  params.system = orc::VideoSystem::PAL;
  params.frame_width_nominal = 1135;
  params.frame_height = 625;
  params.blanking_level = 256;
  params.white_level = 844;

  orc::FrameDescriptor desc;
  desc.height = 625;
  desc.samples_total = 625 * 1135;

  EXPECT_CALL(*mock, get_video_parameters()).WillRepeatedly(Return(params));
  EXPECT_CALL(*mock, get_frame_descriptor(orc::FrameID{0}))
      .WillRepeatedly(Return(desc));

  static const orc::VideoFrameRepresentation::sample_type kLine0[1135] = {};
  // field2_lines=312; output line 312 → source line 0.
  EXPECT_CALL(*mock, get_line(orc::FrameID{0}, 0)).WillOnce(Return(kLine0));

  orc::FrameFieldSwapRepresentation rep(mock);
  const auto* ptr = rep.get_line(orc::FrameID{0}, 312);
  EXPECT_EQ(ptr, kLine0);
}

TEST(FrameFieldSwapRepresentationTest, GetFrame_ReturnsNullptr) {
  auto mock = std::make_shared<MockVideoFrameRepresentation>();
  EXPECT_CALL(*mock, get_video_parameters()).WillRepeatedly(Return(std::nullopt));
  orc::FrameFieldSwapRepresentation rep(mock);
  EXPECT_EQ(rep.get_frame(orc::FrameID{0}), nullptr);
}

TEST(FrameFieldSwapRepresentationTest, TypeName_IsCorrect) {
  auto mock = std::make_shared<MockVideoFrameRepresentation>();
  EXPECT_CALL(*mock, get_video_parameters()).WillRepeatedly(Return(std::nullopt));
  orc::FrameFieldSwapRepresentation rep(mock);
  EXPECT_EQ(rep.type_name(), "frame_field_swap_representation");
}

}  // namespace orc_unit_test
