/*
 * File:        frame_phase_corrector_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for FramePhaseCorrectorStage parameter defaults
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../../../../orc/plugins/stages/frame_phase_corrector/frame_phase_corrector_stage.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../orc/core/include/observation_context.h"
#include "../../mocks/mock_video_frame_representation.h"

using ::testing::Return;
using ::testing::_;

namespace orc_unit_test {

namespace {
const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::string& name) {
  auto it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [&](const orc::ParameterDescriptor& d) { return d.name == name; });
  return it == descriptors.end() ? nullptr : &(*it);
}
}  // namespace

// ── NodeTypeInfo / interface contracts ──────────────────────────────────────

TEST(FramePhaseCorrectorStageTest, RequiredInputCount_IsOne) {
  orc::FramePhaseCorrectorStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(FramePhaseCorrectorStageTest, OutputCount_IsOne) {
  orc::FramePhaseCorrectorStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(FramePhaseCorrectorStageTest, NodeTypeInfo_HasExpectedMetadata) {
  orc::FramePhaseCorrectorStage stage;
  auto info = stage.get_node_type_info();
  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "frame_phase_corrector");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

// ── Parameter descriptors vs runtime defaults ────────────────────────────────

TEST(FramePhaseCorrectorStageTest,
     Descriptor_CorrectFieldSwapMatchRuntimeDefault) {
  orc::FramePhaseCorrectorStage stage;
  const auto descs = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* d = find_descriptor(descs, "correct_field_swap");
  ASSERT_NE(d, nullptr);
  ASSERT_TRUE(d->constraints.default_value.has_value());
  ASSERT_TRUE(std::holds_alternative<bool>(*d->constraints.default_value));
  ASSERT_TRUE(std::holds_alternative<bool>(params.at("correct_field_swap")));
  EXPECT_EQ(std::get<bool>(*d->constraints.default_value),
            std::get<bool>(params.at("correct_field_swap")));
}

TEST(FramePhaseCorrectorStageTest,
     Descriptor_VerifyPhaseSequenceMatchRuntimeDefault) {
  orc::FramePhaseCorrectorStage stage;
  const auto descs = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* d = find_descriptor(descs, "verify_phase_sequence");
  ASSERT_NE(d, nullptr);
  ASSERT_TRUE(d->constraints.default_value.has_value());
  ASSERT_TRUE(std::holds_alternative<bool>(*d->constraints.default_value));
  ASSERT_TRUE(
      std::holds_alternative<bool>(params.at("verify_phase_sequence")));
  EXPECT_EQ(std::get<bool>(*d->constraints.default_value),
            std::get<bool>(params.at("verify_phase_sequence")));
}

// ── set_parameters validation ─────────────────────────────────────────────

TEST(FramePhaseCorrectorStageTest, SetParameters_CorrectFieldSwap_ToFalse) {
  orc::FramePhaseCorrectorStage stage;
  EXPECT_TRUE(stage.set_parameters({{"correct_field_swap", false}}));
  EXPECT_FALSE(
      std::get<bool>(stage.get_parameters().at("correct_field_swap")));
}

TEST(FramePhaseCorrectorStageTest,
     SetParameters_VerifyPhaseSequence_ToFalse) {
  orc::FramePhaseCorrectorStage stage;
  EXPECT_TRUE(stage.set_parameters({{"verify_phase_sequence", false}}));
  EXPECT_FALSE(
      std::get<bool>(stage.get_parameters().at("verify_phase_sequence")));
}

TEST(FramePhaseCorrectorStageTest, SetParameters_RejectsWrongType) {
  orc::FramePhaseCorrectorStage stage;
  EXPECT_FALSE(stage.set_parameters(
      {{"correct_field_swap", std::string("yes")}}));
}

// ── execute() error handling ───────────────────────────────────────────────

TEST(FramePhaseCorrectorStageTest, Execute_ThrowsWhenInputMissing) {
  orc::FramePhaseCorrectorStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({}, {}, ctx), orc::DAGExecutionError);
}

TEST(FramePhaseCorrectorStageTest, Execute_ThrowsWhenInputIsNotVFrameR) {
  orc::FramePhaseCorrectorStage stage;
  orc::ObservationContext ctx;
  struct DummyArtifact : public orc::Artifact {
    DummyArtifact()
        : orc::Artifact(orc::ArtifactID("dummy"), orc::Provenance{}) {}
    std::string type_name() const override { return "dummy"; }
  };
  orc::ArtifactPtr bad = std::make_shared<DummyArtifact>();
  EXPECT_THROW(stage.execute({bad}, {}, ctx), orc::DAGExecutionError);
}

// ── PhaseCorectedRepresentation unit tests ────────────────────────────────

TEST(PhaseCorectedRepresentationTest, TypeName_IsCorrect) {
  auto mock = std::make_shared<MockVideoFrameRepresentation>();
  EXPECT_CALL(*mock, get_video_parameters())
      .WillRepeatedly(Return(std::nullopt));
  orc::PhaseCorectedRepresentation rep(mock, {});
  EXPECT_EQ(rep.type_name(), "phase_corrected_representation");
}

TEST(PhaseCorectedRepresentationTest,
     GetFrame_ForwardsToSourceWhenNoCorrection) {
  auto mock = std::make_shared<MockVideoFrameRepresentation>();
  static const orc::VideoFrameRepresentation::sample_type kBuf[10] = {};

  EXPECT_CALL(*mock, get_video_parameters())
      .WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(*mock, get_frame(orc::FrameID{0})).WillOnce(Return(kBuf));

  orc::PhaseCorectedRepresentation rep(mock, {});
  EXPECT_EQ(rep.get_frame(orc::FrameID{0}), kBuf);
}

TEST(PhaseCorectedRepresentationTest,
     GetFrame_ReturnsNullptrWhenSwapActive) {
  auto mock = std::make_shared<MockVideoFrameRepresentation>();

  orc::SourceParameters sp{};
  sp.system = orc::VideoSystem::PAL;
  sp.frame_width_nominal = 1135;
  sp.frame_height = 625;

  EXPECT_CALL(*mock, get_video_parameters()).WillRepeatedly(Return(sp));

  orc::PhaseCorectedRepresentation::FrameCorrection corr;
  corr.swap_fields = true;
  std::map<orc::FrameID, orc::PhaseCorectedRepresentation::FrameCorrection> m;
  m[orc::FrameID{0}] = corr;

  orc::PhaseCorectedRepresentation rep(mock, std::move(m));
  EXPECT_EQ(rep.get_frame(orc::FrameID{0}), nullptr);
}

TEST(PhaseCorectedRepresentationTest,
     GetFrameDescriptor_ReturnsCorrectedColourIndex) {
  auto mock = std::make_shared<MockVideoFrameRepresentation>();

  orc::FrameDescriptor desc{};
  desc.frame_id = orc::FrameID{5};
  desc.colour_frame_index = 3;

  EXPECT_CALL(*mock, get_video_parameters())
      .WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(*mock, get_frame_descriptor(orc::FrameID{5}))
      .WillRepeatedly(Return(desc));

  orc::PhaseCorectedRepresentation::FrameCorrection corr;
  corr.swap_fields = false;
  corr.corrected_colour_index = 2;
  std::map<orc::FrameID, orc::PhaseCorectedRepresentation::FrameCorrection> m;
  m[orc::FrameID{5}] = corr;

  orc::PhaseCorectedRepresentation rep(mock, std::move(m));
  auto d = rep.get_frame_descriptor(orc::FrameID{5});
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d->colour_frame_index, 2);
}

TEST(PhaseCorectedRepresentationTest,
     GetFrameDescriptor_PassthroughWhenNoEntry) {
  auto mock = std::make_shared<MockVideoFrameRepresentation>();

  orc::FrameDescriptor desc{};
  desc.frame_id = orc::FrameID{7};
  desc.colour_frame_index = 4;

  EXPECT_CALL(*mock, get_video_parameters())
      .WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(*mock, get_frame_descriptor(orc::FrameID{7}))
      .WillRepeatedly(Return(desc));

  orc::PhaseCorectedRepresentation rep(mock, {});
  auto d = rep.get_frame_descriptor(orc::FrameID{7});
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d->colour_frame_index, 4);
}

}  // namespace orc_unit_test
