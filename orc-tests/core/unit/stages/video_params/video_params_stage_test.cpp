/*
 * File:        video_params_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for VideoParamsStage defaults, validation, and
 * overrides
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
#include "../../include/video_field_representation_mock.h"

using testing::Return;

namespace orc_unit_test {
namespace {
const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::string& name) {
  auto it = std::find_if(descriptors.begin(), descriptors.end(),
                         [&](const orc::ParameterDescriptor& descriptor) {
                           return descriptor.name == name;
                         });

  return it == descriptors.end() ? nullptr : &(*it);
}
}  // namespace

TEST(VideoParamsStageTest, requiredInputCountIsOne) {
  orc::VideoParamsStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(VideoParamsStageTest, outputCountIsOne) {
  orc::VideoParamsStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(VideoParamsStageTest, nodeTypeInfoHasExpectedMetadata) {
  orc::VideoParamsStage stage;
  auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "video_params");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(VideoParamsStageTest, descriptorDefaultsMatchRuntimeDefaults) {
  orc::VideoParamsStage stage;
  const auto descriptors = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();
  const std::array<const char*, 8> parameter_names = {
      "colourBurstStart", "colourBurstEnd",       "activeVideoStart",
      "activeVideoEnd",   "firstActiveFieldLine", "lastActiveFieldLine",
      "white16bIRE",      "black16bIRE"};

  for (const auto* name : parameter_names) {
    const auto* descriptor = find_descriptor(descriptors, name);
    ASSERT_NE(descriptor, nullptr);
    if (!descriptor->constraints.default_value.has_value()) {
      FAIL() << "Expected default_value to have a value for " << name;
      return;
    }
    ASSERT_TRUE(std::holds_alternative<int32_t>(
        *descriptor->constraints.default_value));
    ASSERT_TRUE(std::holds_alternative<int32_t>(params.at(name)));
    EXPECT_EQ(std::get<int32_t>(*descriptor->constraints.default_value),
              std::get<int32_t>(params.at(name)));
  }
}

TEST(VideoParamsStageTest, setParametersAcceptsInt32Overrides) {
  orc::VideoParamsStage stage;

  const bool result = stage.set_parameters(
      {{"activeVideoStart", static_cast<int32_t>(120)}, {"black16bIRE", static_cast<int32_t>(1200)}});
  const auto params = stage.get_parameters();

  EXPECT_TRUE(result);
  EXPECT_EQ(std::get<int32_t>(params.at("activeVideoStart")), 120);
  EXPECT_EQ(std::get<int32_t>(params.at("black16bIRE")), 1200);
}

TEST(VideoParamsStageTest, setParametersRejectsUnknownParameter) {
  orc::VideoParamsStage stage;
  EXPECT_FALSE(stage.set_parameters({{"unknown", int32_t(1)}}));
}

TEST(VideoParamsStageTest, setParametersRejectsWrongType) {
  orc::VideoParamsStage stage;
  EXPECT_FALSE(
      stage.set_parameters({{"activeVideoStart", std::string("120")}}));
}

TEST(VideoParamsStageTest, processAppliesConfiguredOverrides) {
  orc::VideoParamsStage stage;
  auto source =
      std::make_shared<testing::NiceMock<MockVideoFieldRepresentation>>();
  orc::SourceParameters source_params;
  source_params.system = orc::VideoSystem::NTSC;
  source_params.field_width = 844;
  source_params.field_height = 263;
  source_params.black_16b_ire = 1000;
  source_params.white_16b_ire = 50000;

  EXPECT_CALL(*source, get_video_parameters())
      .WillRepeatedly(Return(source_params));
  ASSERT_TRUE(stage.set_parameters(
      {{"activeVideoStart", int32_t(120)}, {"black16bIRE", int32_t(1500)}}));

  const auto result = stage.process(source);

  ASSERT_NE(result, nullptr);
  const auto overridden = result->get_video_parameters();
  if (!overridden.has_value()) {
    FAIL() << "Expected overridden to have a value";
    return;
  }
  EXPECT_EQ(overridden->field_width, 844);
  EXPECT_EQ(overridden->field_height, 263);
  EXPECT_EQ(overridden->active_video_start, 120);
  EXPECT_EQ(overridden->black_16b_ire, 1500);
  EXPECT_EQ(overridden->white_16b_ire, 50000);
}

TEST(VideoParamsStageTest, processPreservesPalVideoSystem) {
  orc::VideoParamsStage stage;
  auto source =
      std::make_shared<testing::NiceMock<MockVideoFieldRepresentation>>();
  orc::SourceParameters source_params;
  source_params.system = orc::VideoSystem::PAL;
  source_params.field_width = 922;
  source_params.field_height = 313;
  source_params.first_active_field_line = 22;
  source_params.last_active_field_line = 310;

  EXPECT_CALL(*source, get_video_parameters())
      .WillRepeatedly(Return(source_params));
  ASSERT_TRUE(stage.set_parameters({{"activeVideoStart", int32_t(120)}}));

  const auto result = stage.process(source);

  ASSERT_NE(result, nullptr);
  const auto overridden = result->get_video_parameters();
  if (!overridden.has_value()) {
    FAIL() << "Expected overridden to have a value";
    return;
  }
  EXPECT_EQ(overridden->system, orc::VideoSystem::PAL);
  EXPECT_EQ(overridden->first_active_field_line, 22);
  EXPECT_EQ(overridden->last_active_field_line, 310);
}

TEST(VideoParamsStageTest, processPreservesPalMVideoSystem) {
  orc::VideoParamsStage stage;
  auto source =
      std::make_shared<testing::NiceMock<MockVideoFieldRepresentation>>();
  orc::SourceParameters source_params;
  source_params.system = orc::VideoSystem::PAL_M;
  source_params.field_width = 844;
  source_params.field_height = 262;
  source_params.first_active_field_line = 20;
  source_params.last_active_field_line = 259;
  source_params.black_16b_ire = 0;
  source_params.white_16b_ire = 65535;

  EXPECT_CALL(*source, get_video_parameters())
      .WillRepeatedly(Return(source_params));
  // Override only the first active field line, keeping other PAL-M defaults
  ASSERT_TRUE(stage.set_parameters({{"firstActiveFieldLine", int32_t(21)}}));

  const auto result = stage.process(source);

  ASSERT_NE(result, nullptr);
  const auto overridden = result->get_video_parameters();
  if (!overridden.has_value()) {
    FAIL() << "Expected overridden to have a value";
    return;
  }
  // Verify PAL-M system is preserved
  EXPECT_EQ(overridden->system, orc::VideoSystem::PAL_M);
  // Verify override was applied
  EXPECT_EQ(overridden->first_active_field_line, 21);
  // Verify non-overridden PAL-M defaults are preserved
  EXPECT_EQ(overridden->last_active_field_line, 259);
  EXPECT_EQ(overridden->black_16b_ire, 0);
  EXPECT_EQ(overridden->white_16b_ire, 65535);
}

TEST(VideoParamsStageTest, executeThrowsWhenInputMissing) {
  orc::VideoParamsStage stage;
  orc::ObservationContext observation_context;

  EXPECT_THROW(stage.execute({}, {}, observation_context),
               orc::DAGExecutionError);
}
}  // namespace orc_unit_test