/*
 * File:        dropout_map_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for DropoutMapStage defaults and lightweight
 * validation paths
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/dropout_map/dropout_map_stage.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../orc/core/include/observation_context.h"

namespace orc_unit_test {
namespace {
class FakeArtifact : public orc::Artifact {
 public:
  FakeArtifact() : Artifact(orc::ArtifactID("fake"), orc::Provenance{}) {}

  std::string type_name() const override { return "fake_artifact"; }
};
}  // namespace

TEST(DropoutMapStageTest, requiredInputCountIsOne) {
  orc::DropoutMapStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(DropoutMapStageTest, outputCountIsOne) {
  orc::DropoutMapStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(DropoutMapStageTest, nodeTypeInfoHasExpectedMetadata) {
  orc::DropoutMapStage stage;
  auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "dropout_map");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(DropoutMapStageTest, descriptorDefaultMatchesRuntimeDefault) {
  orc::DropoutMapStage stage;
  const auto descriptors = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();
  auto it = std::find_if(descriptors.begin(), descriptors.end(),
                         [](const orc::ParameterDescriptor& descriptor) {
                           return descriptor.name == "dropout_map";
                         });

  ASSERT_NE(it, descriptors.end());
  if (!it->constraints.default_value.has_value()) {
    FAIL() << "Expected default_value to have a value";
    return;
  }
  ASSERT_TRUE(
      std::holds_alternative<std::string>(*it->constraints.default_value));
  ASSERT_TRUE(std::holds_alternative<std::string>(params.at("dropout_map")));
  EXPECT_EQ(std::get<std::string>(*it->constraints.default_value),
            std::get<std::string>(params.at("dropout_map")));
}

TEST(DropoutMapStageTest, setParametersAcceptsDropoutMapString) {
  orc::DropoutMapStage stage;

  const bool result = stage.set_parameters(
      {{"dropout_map",
        std::string(
            "[{field:0,add:[{line:10,start:100,end:200}],remove:[]}]")}});
  const auto params = stage.get_parameters();

  EXPECT_TRUE(result);
  EXPECT_EQ(std::get<std::string>(params.at("dropout_map")),
            "[{field:0,add:[{line:10,start:100,end:200}],remove:[]}]");
}

TEST(DropoutMapStageTest, executeThrowsWhenInputCountIsNotOne) {
  orc::DropoutMapStage stage;
  orc::ObservationContext observation_context;

  EXPECT_THROW(stage.execute({}, {}, observation_context), std::runtime_error);
}

TEST(DropoutMapStageTest, executeThrowsWhenInputIsWrongArtifactType) {
  orc::DropoutMapStage stage;
  orc::ObservationContext observation_context;
  std::vector<orc::ArtifactPtr> inputs = {std::make_shared<FakeArtifact>()};

  EXPECT_THROW(stage.execute(inputs, {}, observation_context),
               std::runtime_error);
}
}  // namespace orc_unit_test