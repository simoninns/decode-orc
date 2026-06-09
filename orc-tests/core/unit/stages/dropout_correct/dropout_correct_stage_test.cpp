/*
 * File:        dropout_correct_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for DropoutCorrectStage defaults, validation, and
 * lightweight execute paths
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/dropout_correct/dropout_correct_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

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

TEST(DropoutCorrectStageTest, requiredInputCountIsOne) {
  orc::DropoutCorrectStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(DropoutCorrectStageTest, outputCountIsOne) {
  orc::DropoutCorrectStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(DropoutCorrectStageTest, nodeTypeInfoHasExpectedMetadata) {
  orc::DropoutCorrectStage stage;
  auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "dropout_correct");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(DropoutCorrectStageTest, descriptorDefaultsMatchRuntimeDefaults) {
  orc::DropoutCorrectStage stage;
  const auto descriptors = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* overcorrect =
      find_descriptor(descriptors, "overcorrect_extension");
  const auto* intrafield = find_descriptor(descriptors, "intrafield_only");
  const auto* max_distance =
      find_descriptor(descriptors, "max_replacement_distance");
  const auto* match_phase = find_descriptor(descriptors, "match_chroma_phase");
  const auto* highlight = find_descriptor(descriptors, "highlight_corrections");

  ASSERT_NE(overcorrect, nullptr);
  ASSERT_NE(intrafield, nullptr);
  ASSERT_NE(max_distance, nullptr);
  ASSERT_NE(match_phase, nullptr);
  ASSERT_NE(highlight, nullptr);

  if (!overcorrect->constraints.default_value.has_value() ||
      !intrafield->constraints.default_value.has_value() ||
      !max_distance->constraints.default_value.has_value() ||
      !match_phase->constraints.default_value.has_value() ||
      !highlight->constraints.default_value.has_value()) {
    FAIL() << "Expected all descriptors to have default values";
    return;
  }

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

TEST(DropoutCorrectStageTest, setParametersAcceptsValidValues) {
  orc::DropoutCorrectStage stage;

  const bool result =
      stage.set_parameters({{"overcorrect_extension", static_cast<uint32_t>(8)},
                            {"intrafield_only", true},
                            {"max_replacement_distance", static_cast<uint32_t>(12)},
                            {"match_chroma_phase", false},
                            {"highlight_corrections", true}});
  const auto params = stage.get_parameters();

  EXPECT_TRUE(result);
  EXPECT_EQ(std::get<uint32_t>(params.at("overcorrect_extension")), 8u);
  EXPECT_TRUE(std::get<bool>(params.at("intrafield_only")));
  EXPECT_EQ(std::get<uint32_t>(params.at("max_replacement_distance")), 12u);
  EXPECT_FALSE(std::get<bool>(params.at("match_chroma_phase")));
  EXPECT_TRUE(std::get<bool>(params.at("highlight_corrections")));
}

TEST(DropoutCorrectStageTest, setParametersRejectsOutOfRangeValue) {
  orc::DropoutCorrectStage stage;
  EXPECT_FALSE(stage.set_parameters({{"overcorrect_extension", uint32_t(49)}}));
}

TEST(DropoutCorrectStageTest, setParametersRejectsUnknownParameter) {
  orc::DropoutCorrectStage stage;
  EXPECT_FALSE(stage.set_parameters({{"unknown", true}}));
}

TEST(DropoutCorrectStageTest, executeReturnsSourceWhenFieldRangeInvalid) {
  orc::DropoutCorrectStage stage;
  orc::ObservationContext observation_context;
  auto source =
      std::make_shared<testing::NiceMock<MockVideoFieldRepresentation>>();
  std::vector<orc::ArtifactPtr> inputs = {
      std::static_pointer_cast<orc::Artifact>(source)};

  EXPECT_CALL(*source, type_name()).WillRepeatedly(Return("mock_vfr"));
  EXPECT_CALL(*source, field_range())
      .WillRepeatedly(Return(orc::FieldIDRange{}));

  const auto outputs = stage.execute(inputs, {}, observation_context);

  ASSERT_EQ(outputs.size(), 1u);
  EXPECT_EQ(outputs[0].get(), source.get());
}
}  // namespace orc_unit_test