/*
 * File:        field_map_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for FieldMapStage parameter defaults and validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/field_map/field_map_stage.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../orc/core/include/observation_context.h"
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

TEST(FieldMapStageTest, requiredInputCountIsOne) {
  orc::FieldMapStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(FieldMapStageTest, outputCountIsOne) {
  orc::FieldMapStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(FieldMapStageTest, nodeTypeInfoHasExpectedMetadata) {
  orc::FieldMapStage stage;
  auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "field_map");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(FieldMapStageTest, parameterDescriptorsRangesMatchRuntimeDefault) {
  orc::FieldMapStage stage;
  const auto descriptors = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* descriptor = find_descriptor(descriptors, "ranges");
  ASSERT_NE(descriptor, nullptr);
  if (!descriptor->constraints.default_value.has_value()) {
    FAIL() << "Expected default_value to have a value";
    return;
  }
  ASSERT_TRUE(std::holds_alternative<std::string>(
      *descriptor->constraints.default_value));
  ASSERT_TRUE(std::holds_alternative<std::string>(params.at("ranges")));
  EXPECT_EQ(std::get<std::string>(*descriptor->constraints.default_value),
            std::get<std::string>(params.at("ranges")));
}

TEST(FieldMapStageTest, parameterDescriptorsSeedMatchRuntimeDefault) {
  orc::FieldMapStage stage;
  const auto descriptors = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* descriptor = find_descriptor(descriptors, "seed");
  ASSERT_NE(descriptor, nullptr);
  if (!descriptor->constraints.default_value.has_value()) {
    FAIL() << "Expected default_value to have a value";
    return;
  }
  ASSERT_TRUE(
      std::holds_alternative<int32_t>(*descriptor->constraints.default_value));
  ASSERT_TRUE(std::holds_alternative<int32_t>(params.at("seed")));
  EXPECT_EQ(std::get<int32_t>(*descriptor->constraints.default_value),
            std::get<int32_t>(params.at("seed")));
}

TEST(FieldMapStageTest, setParametersAcceptsValidRangesAndSeed) {
  orc::FieldMapStage stage;

  const bool result = stage.set_parameters(
      {{"ranges", std::string("0-10,20-30")}, {"seed", static_cast<int32_t>(9)}});
  const auto params = stage.get_parameters();

  EXPECT_TRUE(result);
  EXPECT_EQ(std::get<std::string>(params.at("ranges")), "0-10,20-30");
  EXPECT_EQ(std::get<int32_t>(params.at("seed")), 9);
}

TEST(FieldMapStageTest, setParametersRejectsInvalidRangeSpecification) {
  orc::FieldMapStage stage;
  EXPECT_FALSE(stage.set_parameters({{"ranges", std::string("not-a-range")}}));
}

TEST(FieldMapStageTest, setParametersRejectsWrongSeedType) {
  orc::FieldMapStage stage;
  EXPECT_FALSE(stage.set_parameters({{"seed", std::string("bad")}}));
}

TEST(FieldMapStageTest, executeReturnsInputUnchangedWhenRangesAreEmpty) {
  orc::FieldMapStage stage;
  orc::ObservationContext observation_context;
  auto source = std::make_shared<MockVideoFieldRepresentation>();
  std::vector<orc::ArtifactPtr> inputs = {
      std::static_pointer_cast<orc::Artifact>(source)};

  const auto outputs = stage.execute(inputs, {}, observation_context);

  ASSERT_EQ(outputs.size(), 1u);
  EXPECT_EQ(outputs[0].get(), source.get());
}
}  // namespace orc_unit_test