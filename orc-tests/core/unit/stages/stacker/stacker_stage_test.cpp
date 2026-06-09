/*
 * File:        stacker_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for StackerStage defaults and parameter validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/stacker/stacker_stage.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "../../include/video_field_representation_mock.h"

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

TEST(StackerStageTest, requiredInputCountIsOne) {
  orc::StackerStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(StackerStageTest, outputCountIsOne) {
  orc::StackerStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(StackerStageTest, nodeTypeInfoHasExpectedMetadata) {
  orc::StackerStage stage;
  auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::MERGER);
  EXPECT_EQ(info.stage_name, "stacker");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(StackerStageTest, descriptorDefaultsMatchRuntimeDefaults) {
  orc::StackerStage stage;
  const auto descriptors = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* mode = find_descriptor(descriptors, "mode");
  const auto* threshold = find_descriptor(descriptors, "smart_threshold");
  const auto* no_diff_dod = find_descriptor(descriptors, "no_diff_dod");
  const auto* passthrough = find_descriptor(descriptors, "passthrough");
  const auto* audio_stacking = find_descriptor(descriptors, "audio_stacking");
  const auto* efm_stacking = find_descriptor(descriptors, "efm_stacking");

  ASSERT_NE(mode, nullptr);
  ASSERT_NE(threshold, nullptr);
  ASSERT_NE(no_diff_dod, nullptr);
  ASSERT_NE(passthrough, nullptr);
  ASSERT_NE(audio_stacking, nullptr);
  ASSERT_NE(efm_stacking, nullptr);

  if (!mode->constraints.default_value.has_value() ||
      !threshold->constraints.default_value.has_value() ||
      !no_diff_dod->constraints.default_value.has_value() ||
      !passthrough->constraints.default_value.has_value() ||
      !audio_stacking->constraints.default_value.has_value() ||
      !efm_stacking->constraints.default_value.has_value()) {
    FAIL() << "Expected all descriptors to have default values";
    return;
  }

  EXPECT_EQ(std::get<std::string>(*mode->constraints.default_value),
            std::get<std::string>(params.at("mode")));
  EXPECT_EQ(std::get<int32_t>(*threshold->constraints.default_value),
            std::get<int32_t>(params.at("smart_threshold")));
  EXPECT_EQ(std::get<bool>(*no_diff_dod->constraints.default_value),
            std::get<bool>(params.at("no_diff_dod")));
  EXPECT_EQ(std::get<bool>(*passthrough->constraints.default_value),
            std::get<bool>(params.at("passthrough")));
  EXPECT_EQ(std::get<std::string>(*audio_stacking->constraints.default_value),
            std::get<std::string>(params.at("audio_stacking")));
  EXPECT_EQ(std::get<std::string>(*efm_stacking->constraints.default_value),
            std::get<std::string>(params.at("efm_stacking")));
}

TEST(StackerStageTest, setParametersAcceptsValidStringValues) {
  orc::StackerStage stage;

  const bool result =
      stage.set_parameters({{"mode", std::string("Smart Mean")},
                            {"smart_threshold", static_cast<int32_t>(17)},
                            {"no_diff_dod", true},
                            {"passthrough", true},
                            {"audio_stacking", std::string("Median")},
                            {"efm_stacking", std::string("Disabled")}});
  const auto params = stage.get_parameters();

  EXPECT_TRUE(result);
  EXPECT_EQ(std::get<std::string>(params.at("mode")), "Smart Mean");
  EXPECT_EQ(std::get<int32_t>(params.at("smart_threshold")), 17);
  EXPECT_TRUE(std::get<bool>(params.at("no_diff_dod")));
  EXPECT_TRUE(std::get<bool>(params.at("passthrough")));
  EXPECT_EQ(std::get<std::string>(params.at("audio_stacking")), "Median");
  EXPECT_EQ(std::get<std::string>(params.at("efm_stacking")), "Disabled");
}

TEST(StackerStageTest, setParametersAcceptsLegacyIntegerMode) {
  orc::StackerStage stage;

  ASSERT_TRUE(stage.set_parameters({{"mode", int32_t(2)}}));

  EXPECT_EQ(std::get<std::string>(stage.get_parameters().at("mode")),
            "Smart Mean");
}

TEST(StackerStageTest, setParametersRejectsInvalidMode) {
  orc::StackerStage stage;
  EXPECT_FALSE(stage.set_parameters({{"mode", std::string("Nope")}}));
}

TEST(StackerStageTest, setParametersRejectsThresholdOutsideBounds) {
  orc::StackerStage stage;
  EXPECT_FALSE(stage.set_parameters({{"smart_threshold", int32_t(129)}}));
}

TEST(StackerStageTest, processReturnsNullWhenSourcesEmpty) {
  orc::StackerStage stage;
  EXPECT_EQ(stage.process({}), nullptr);
}

TEST(StackerStageTest, processReturnsOnlySourceInPassthroughMode) {
  orc::StackerStage stage;
  auto source = std::make_shared<MockVideoFieldRepresentation>();
  std::vector<std::shared_ptr<const orc::VideoFieldRepresentation>> sources = {
      source};

  const auto result = stage.process(sources);

  EXPECT_EQ(result.get(), source.get());
}
}  // namespace orc_unit_test