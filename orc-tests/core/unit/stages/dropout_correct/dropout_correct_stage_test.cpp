/*
 * File:        dropout_correct_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for DropoutCorrectStage (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/dropout_correct/dropout_correct_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../orc/core/include/observation_context.h"
#include "../../include/video_frame_representation_artifact_mock.h"

using ::testing::NiceMock;
using ::testing::Return;

namespace orc_unit_test {
namespace {

const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::string& name) {
  auto it = std::find_if(descriptors.begin(), descriptors.end(),
                         [&](const orc::ParameterDescriptor& d) {
                           return d.name == name;
                         });
  return it == descriptors.end() ? nullptr : &(*it);
}

}  // namespace

TEST(DropoutCorrectStageTest, RequiredInputCount_IsOne) {
  orc::DropoutCorrectStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(DropoutCorrectStageTest, OutputCount_IsOne) {
  orc::DropoutCorrectStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(DropoutCorrectStageTest, NodeTypeInfo_HasExpectedMetadata) {
  orc::DropoutCorrectStage stage;
  auto info = stage.get_node_type_info();
  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "dropout_correct");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(DropoutCorrectStageTest, Descriptor_DefaultsMatchRuntimeDefaults) {
  orc::DropoutCorrectStage stage;
  const auto descriptors = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* overcorrect = find_descriptor(descriptors, "overcorrect_extension");
  const auto* intrafield = find_descriptor(descriptors, "intrafield_only");
  const auto* max_distance = find_descriptor(descriptors, "max_replacement_distance");
  const auto* match_phase = find_descriptor(descriptors, "match_chroma_phase");
  const auto* highlight = find_descriptor(descriptors, "highlight_corrections");

  ASSERT_NE(overcorrect, nullptr);
  ASSERT_NE(intrafield, nullptr);
  ASSERT_NE(max_distance, nullptr);
  ASSERT_NE(match_phase, nullptr);
  ASSERT_NE(highlight, nullptr);

  ASSERT_TRUE(overcorrect->constraints.default_value.has_value());
  ASSERT_TRUE(intrafield->constraints.default_value.has_value());
  ASSERT_TRUE(max_distance->constraints.default_value.has_value());
  ASSERT_TRUE(match_phase->constraints.default_value.has_value());
  ASSERT_TRUE(highlight->constraints.default_value.has_value());

  EXPECT_EQ(std::get<uint32_t>(*overcorrect->constraints.default_value),
            std::get<uint32_t>(params.at("overcorrect_extension")));
  EXPECT_EQ(std::get<bool>(*intrafield->constraints.default_value),
            std::get<bool>(params.at("intrafield_only")));
  EXPECT_EQ(std::get<uint32_t>(*max_distance->constraints.default_value),
            std::get<uint32_t>(params.at("max_replacement_distance")));
  EXPECT_EQ(std::get<bool>(*match_phase->constraints.default_value),
            std::get<bool>(params.at("match_chroma_phase")));
  EXPECT_EQ(std::get<bool>(*highlight->constraints.default_value),
            std::get<bool>(params.at("highlight_corrections")));
}

TEST(DropoutCorrectStageTest, SetParameters_AcceptsValidValues) {
  orc::DropoutCorrectStage stage;

  const bool result = stage.set_parameters(
      {{"overcorrect_extension", static_cast<uint32_t>(8)},
       {"intrafield_only", true},
       {"max_replacement_distance", static_cast<uint32_t>(12)},
       {"match_chroma_phase", false},
       {"highlight_corrections", true}});

  EXPECT_TRUE(result);

  const auto params = stage.get_parameters();
  EXPECT_EQ(std::get<uint32_t>(params.at("overcorrect_extension")), 8u);
  EXPECT_TRUE(std::get<bool>(params.at("intrafield_only")));
  EXPECT_EQ(std::get<uint32_t>(params.at("max_replacement_distance")), 12u);
  EXPECT_FALSE(std::get<bool>(params.at("match_chroma_phase")));
  EXPECT_TRUE(std::get<bool>(params.at("highlight_corrections")));
}

TEST(DropoutCorrectStageTest, SetParameters_RejectsOutOfRangeOvercorrect) {
  orc::DropoutCorrectStage stage;
  EXPECT_FALSE(stage.set_parameters(
      {{"overcorrect_extension", static_cast<uint32_t>(49)}}));
}

TEST(DropoutCorrectStageTest, SetParameters_RejectsUnknownParameter) {
  orc::DropoutCorrectStage stage;
  EXPECT_FALSE(stage.set_parameters({{"unknown", true}}));
}

TEST(DropoutCorrectStageTest, Execute_ThrowsWhenNoInputProvided) {
  orc::DropoutCorrectStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({}, {}, ctx), orc::DAGExecutionError);
}

TEST(DropoutCorrectStageTest, Execute_ThrowsWhenInputIsWrongType) {
  struct FakeArt : public orc::Artifact {
    FakeArt() : Artifact(orc::ArtifactID("x"), orc::Provenance{}) {}
    std::string type_name() const override { return "x"; }
  };
  orc::DropoutCorrectStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({std::make_shared<FakeArt>()}, {}, ctx),
               orc::DAGExecutionError);
}

TEST(DropoutCorrectStageTest, Execute_ReturnsVFrameRWhenInputIsValid) {
  orc::DropoutCorrectStage stage;
  orc::ObservationContext ctx;
  auto source = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  ON_CALL(*source, type_name()).WillByDefault(Return("test_vfr"));
  ON_CALL(*source, frame_range())
      .WillByDefault(Return(orc::FrameIDRange{0u, 9u}));
  ON_CALL(*source, frame_count()).WillByDefault(Return(10u));

  const auto outputs = stage.execute({source}, {}, ctx);

  ASSERT_EQ(outputs.size(), 1u);
  auto vfr =
      std::dynamic_pointer_cast<orc::VideoFrameRepresentation>(outputs[0]);
  EXPECT_NE(vfr, nullptr);
  // Output wraps the input so it must be a different object
  EXPECT_NE(outputs[0].get(), source.get());
}

TEST(DropoutCorrectStageTest, RunsToLineDropouts_SplitsAcrossLines) {
  // A run of 10 samples starting at sample 5 on line 0 (SPL=10)
  // Line 0: samples 5–9 (5 samples), line 1: samples 0–4 (5 samples)
  std::vector<orc::DropoutRun> runs = {
      {0u, 5u, 10u, 0u}  // frame_id=0, start=5, count=10
  };
  auto lds = orc::DropoutCorrectStage::runs_to_line_dropouts(runs, 10);
  ASSERT_EQ(lds.size(), 2u);
  EXPECT_EQ(lds[0].line, 0u);
  EXPECT_EQ(lds[0].start_sample, 5u);
  EXPECT_EQ(lds[0].end_sample, 9u);
  EXPECT_EQ(lds[1].line, 1u);
  EXPECT_EQ(lds[1].start_sample, 0u);
  EXPECT_EQ(lds[1].end_sample, 4u);
}

TEST(DropoutCorrectStageTest, RunsToLineDropouts_WholeLineDropout) {
  std::vector<orc::DropoutRun> runs = {
      {0u, 0u, 910u, 0u}  // exactly one NTSC line
  };
  auto lds = orc::DropoutCorrectStage::runs_to_line_dropouts(runs, 910);
  ASSERT_EQ(lds.size(), 1u);
  EXPECT_EQ(lds[0].line, 0u);
  EXPECT_EQ(lds[0].start_sample, 0u);
  EXPECT_EQ(lds[0].end_sample, 909u);
}

TEST(DropoutCorrectStageTest, RunsToLineDropouts_EmptyRunsReturnsEmpty) {
  auto lds = orc::DropoutCorrectStage::runs_to_line_dropouts({}, 910);
  EXPECT_TRUE(lds.empty());
}

}  // namespace orc_unit_test
