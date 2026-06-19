/*
 * File:        mask_line_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for MaskLineStage (VFrameR, frame-flat line
 * addressing)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/mask_line/mask_line_stage.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "../../mocks/mock_video_frame_representation.h"

using ::testing::_;
using ::testing::Return;

namespace orc_unit_test {
namespace {

const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descs,
    const std::string& name) {
  auto it = std::find_if(
      descs.begin(), descs.end(),
      [&](const orc::ParameterDescriptor& d) { return d.name == name; });
  return it == descs.end() ? nullptr : &(*it);
}

orc::SourceParameters make_ntsc_params() {
  orc::SourceParameters p;
  p.system = orc::VideoSystem::NTSC;
  p.frame_width_nominal = 910;
  p.frame_height = 525;
  p.blanking_level = 240;
  p.white_level = 800;
  return p;
}

}  // namespace

TEST(MaskLineStageTest, RequiredInputCount_IsOne) {
  orc::MaskLineStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(MaskLineStageTest, OutputCount_IsOne) {
  orc::MaskLineStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(MaskLineStageTest, NodeTypeInfo_HasExpectedMetadata) {
  orc::MaskLineStage stage;
  auto info = stage.get_node_type_info();
  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "mask_line");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(MaskLineStageTest, Descriptor_DefaultsMatchRuntimeDefaults) {
  orc::MaskLineStage stage;
  const auto descs = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* line_spec = find_descriptor(descs, "lineSpec");
  const auto* mask_sample = find_descriptor(descs, "maskSampleLevel");

  ASSERT_NE(line_spec, nullptr);
  ASSERT_NE(mask_sample, nullptr);
  ASSERT_TRUE(line_spec->constraints.default_value.has_value());
  ASSERT_TRUE(mask_sample->constraints.default_value.has_value());

  EXPECT_EQ(std::get<std::string>(*line_spec->constraints.default_value),
            std::get<std::string>(params.at("lineSpec")));
  EXPECT_EQ(std::get<int32_t>(*mask_sample->constraints.default_value),
            std::get<int32_t>(params.at("maskSampleLevel")));
}

TEST(MaskLineStageTest, SetParameters_AcceptsValidValues) {
  orc::MaskLineStage stage;
  EXPECT_TRUE(stage.set_parameters({{"lineSpec", std::string("21,334")},
                                    {"maskSampleLevel", int32_t{512}}}));
  const auto params = stage.get_parameters();
  EXPECT_EQ(std::get<std::string>(params.at("lineSpec")), "21,334");
  EXPECT_EQ(std::get<int32_t>(params.at("maskSampleLevel")), 512);
}

TEST(MaskLineStageTest, SetParameters_IgnoresWrongTypes) {
  orc::MaskLineStage stage;
  EXPECT_TRUE(stage.set_parameters(
      {{"lineSpec", 7.0}, {"maskSampleLevel", std::string("bad")}}));
  const auto params = stage.get_parameters();
  EXPECT_EQ(std::get<std::string>(params.at("lineSpec")), "");
  EXPECT_EQ(std::get<int32_t>(params.at("maskSampleLevel")), 0);
}

TEST(MaskLineStageTest, Process_ReturnsSourceWhenNoLinesConfigured) {
  orc::MaskLineStage stage;
  auto mock = std::make_shared<MockVideoFrameRepresentation>();
  EXPECT_CALL(*mock, get_video_parameters())
      .WillRepeatedly(Return(std::nullopt));

  const auto result = stage.process(mock);
  EXPECT_EQ(result.get(), mock.get());
}

TEST(MaskLineStageTest, Process_WrapsSourceWhenMaskingConfigured) {
  orc::MaskLineStage stage;
  auto mock = std::make_shared<MockVideoFrameRepresentation>();
  EXPECT_CALL(*mock, get_video_parameters())
      .WillRepeatedly(Return(std::nullopt));

  ASSERT_TRUE(stage.set_parameters(
      {{"lineSpec", std::string("21")}, {"maskSampleLevel", int32_t{0}}}));
  const auto result = stage.process(mock);

  ASSERT_NE(result, nullptr);
  EXPECT_NE(result.get(), mock.get());
}

TEST(MaskLineStageTest, MaskedRepresentation_MasksSpecifiedLine) {
  orc::MaskLineStage stage;
  auto mock = std::make_shared<MockVideoFrameRepresentation>();

  const auto params = make_ntsc_params();
  orc::FrameDescriptor desc;
  desc.height = 525;
  desc.samples_total = 525 * 910;

  static const orc::VideoFrameRepresentation::sample_type kSourceLine[910] = {};
  const orc::FrameID fid{0};

  EXPECT_CALL(*mock, get_video_parameters()).WillRepeatedly(Return(params));
  EXPECT_CALL(*mock, get_frame_descriptor(fid)).WillRepeatedly(Return(desc));

  // Line 21 should NOT be forwarded to source (it's masked).
  EXPECT_CALL(*mock, get_line(fid, 21)).Times(0);
  // Line 20 (not masked) should be forwarded.
  EXPECT_CALL(*mock, get_line(fid, 20)).WillOnce(Return(kSourceLine));

  ASSERT_TRUE(stage.set_parameters(
      {{"lineSpec", std::string("21")}, {"maskSampleLevel", int32_t{0}}}));
  const auto masked = stage.process(mock);
  ASSERT_NE(masked, nullptr);

  // Masked line 21: maskSampleLevel=0 → sample value 0 (sync tip).
  const auto* masked_ptr = masked->get_line(fid, 21);
  ASSERT_NE(masked_ptr, nullptr);
  EXPECT_EQ(masked_ptr[0], static_cast<int16_t>(0));

  // Unmasked line 20: should forward to source.
  const auto* pass_ptr = masked->get_line(fid, 20);
  EXPECT_EQ(pass_ptr, kSourceLine);
}

TEST(MaskLineStageTest, Execute_ThrowsWhenInputMissing) {
  orc::MaskLineStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({}, {}, ctx), orc::DAGExecutionError);
}

}  // namespace orc_unit_test
