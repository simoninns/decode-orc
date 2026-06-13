/*
 * File:        video_params_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for VideoParamsStage (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/video_params/video_params_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "../../../../orc/core/include/observation_context.h"
#include "../../mocks/mock_video_frame_representation.h"

using testing::Return;

namespace orc_unit_test {
namespace {

const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descs, const std::string& name) {
  auto it = std::find_if(descs.begin(), descs.end(),
                         [&](const orc::ParameterDescriptor& d) {
                           return d.name == name;
                         });
  return it == descs.end() ? nullptr : &(*it);
}

}  // namespace

TEST(VideoParamsStageTest, RequiredInputCount_IsOne) {
  orc::VideoParamsStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(VideoParamsStageTest, OutputCount_IsOne) {
  orc::VideoParamsStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(VideoParamsStageTest, NodeTypeInfo_HasExpectedMetadata) {
  orc::VideoParamsStage stage;
  auto info = stage.get_node_type_info();
  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "video_params");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(VideoParamsStageTest, Descriptor_DefaultsMatchRuntimeDefaults) {
  orc::VideoParamsStage stage;
  const auto descs = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();
  const std::array<const char*, 6> names = {
      "activeVideoStart", "activeVideoEnd",
      "firstActiveFrameLine", "lastActiveFrameLine",
      "whiteLevel", "blackLevel"};

  for (const auto* name : names) {
    const auto* d = find_descriptor(descs, name);
    ASSERT_NE(d, nullptr) << "missing descriptor for " << name;
    ASSERT_TRUE(d->constraints.default_value.has_value());
    ASSERT_TRUE(std::holds_alternative<int32_t>(*d->constraints.default_value));
    ASSERT_TRUE(std::holds_alternative<int32_t>(params.at(name)));
    EXPECT_EQ(std::get<int32_t>(*d->constraints.default_value),
              std::get<int32_t>(params.at(name)));
  }
}

TEST(VideoParamsStageTest, SetParameters_AcceptsInt32Overrides) {
  orc::VideoParamsStage stage;
  EXPECT_TRUE(stage.set_parameters(
      {{"activeVideoStart", int32_t(120)}, {"blackLevel", int32_t(282)}}));
  const auto params = stage.get_parameters();
  EXPECT_EQ(std::get<int32_t>(params.at("activeVideoStart")), 120);
  EXPECT_EQ(std::get<int32_t>(params.at("blackLevel")), 282);
}

TEST(VideoParamsStageTest, SetParameters_RejectsUnknownParameter) {
  orc::VideoParamsStage stage;
  EXPECT_FALSE(stage.set_parameters({{"unknown", int32_t(1)}}));
}

TEST(VideoParamsStageTest, SetParameters_RejectsWrongType) {
  orc::VideoParamsStage stage;
  EXPECT_FALSE(stage.set_parameters({{"activeVideoStart", std::string("120")}}));
}

TEST(VideoParamsStageTest, Process_AppliesConfiguredOverrides) {
  orc::VideoParamsStage stage;
  auto source = std::make_shared<testing::NiceMock<MockVideoFrameRepresentation>>();
  orc::SourceParameters src;
  src.system = orc::VideoSystem::NTSC;
  src.frame_width_nominal = 910;
  src.frame_height = 525;
  src.black_level = 240;
  src.white_level = 800;
  src.active_video_start = 100;

  EXPECT_CALL(*source, get_video_parameters()).WillRepeatedly(Return(src));
  ASSERT_TRUE(stage.set_parameters(
      {{"activeVideoStart", int32_t(120)}, {"blackLevel", int32_t(250)}}));

  const auto result = stage.process(source);
  ASSERT_NE(result, nullptr);
  const auto ov = result->get_video_parameters();
  ASSERT_TRUE(ov.has_value());
  EXPECT_EQ(ov->active_video_start, 120);
  EXPECT_EQ(ov->black_level, 250);
  EXPECT_EQ(ov->white_level, 800);        // inherited
  EXPECT_TRUE(ov->has_nonstandard_values); // level override sets this flag
}

TEST(VideoParamsStageTest, Process_PreservesPalVideoSystem) {
  orc::VideoParamsStage stage;
  auto source = std::make_shared<testing::NiceMock<MockVideoFrameRepresentation>>();
  orc::SourceParameters src;
  src.system = orc::VideoSystem::PAL;
  src.frame_width_nominal = 1135;
  src.frame_height = 625;
  src.first_active_frame_line = 44;
  src.last_active_frame_line = 619;

  EXPECT_CALL(*source, get_video_parameters()).WillRepeatedly(Return(src));
  ASSERT_TRUE(stage.set_parameters({{"activeVideoStart", int32_t(120)}}));

  const auto result = stage.process(source);
  ASSERT_NE(result, nullptr);
  const auto ov = result->get_video_parameters();
  ASSERT_TRUE(ov.has_value());
  EXPECT_EQ(ov->system, orc::VideoSystem::PAL);
  EXPECT_EQ(ov->first_active_frame_line, 44);  // inherited
  EXPECT_EQ(ov->last_active_frame_line, 619);  // inherited
}

TEST(VideoParamsStageTest, Process_SetsActiveCroppingFlag_WhenGeometryOverridden) {
  orc::VideoParamsStage stage;
  auto source = std::make_shared<testing::NiceMock<MockVideoFrameRepresentation>>();
  orc::SourceParameters src;
  src.system = orc::VideoSystem::PAL;
  src.frame_width_nominal = 1135;
  src.frame_height = 625;

  EXPECT_CALL(*source, get_video_parameters()).WillRepeatedly(Return(src));
  ASSERT_TRUE(stage.set_parameters({{"firstActiveFrameLine", int32_t(44)}}));

  const auto result = stage.process(source);
  ASSERT_NE(result, nullptr);
  const auto ov = result->get_video_parameters();
  ASSERT_TRUE(ov.has_value());
  EXPECT_TRUE(ov->active_area_cropping_applied);
}

TEST(VideoParamsStageTest, Execute_ThrowsWhenInputMissing) {
  orc::VideoParamsStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({}, {}, ctx), orc::DAGExecutionError);
}

}  // namespace orc_unit_test
