/*
 * File:        field_invert_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for FieldInvertStage invariants and lightweight
 * behavior
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/field_invert/field_invert_stage.h"

#include <gtest/gtest.h>

#include "../../../../orc/core/include/observation_context.h"
#include "../../include/video_field_representation_mock.h"

namespace orc_unit_test {
TEST(FieldInvertStageTest, requiredInputCountIsOne) {
  orc::FieldInvertStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(FieldInvertStageTest, outputCountIsOne) {
  orc::FieldInvertStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(FieldInvertStageTest, nodeTypeInfoHasExpectedMetadata) {
  orc::FieldInvertStage stage;
  auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "field_invert");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(FieldInvertStageTest, parameterDescriptorsAreEmpty) {
  orc::FieldInvertStage stage;
  EXPECT_TRUE(stage.get_parameter_descriptors().empty());
}

TEST(FieldInvertStageTest, getParametersReturnsEmptyMap) {
  orc::FieldInvertStage stage;
  EXPECT_TRUE(stage.get_parameters().empty());
}

TEST(FieldInvertStageTest, setParametersAcceptsIgnoredValues) {
  orc::FieldInvertStage stage;
  EXPECT_TRUE(stage.set_parameters({{"ignored", int32_t(7)}}));
}

TEST(FieldInvertStageTest, processReturnsNullWhenSourceNull) {
  orc::FieldInvertStage stage;
  EXPECT_EQ(stage.process(nullptr), nullptr);
}

TEST(FieldInvertStageTest, processWrapsSourceWhenProvided) {
  orc::FieldInvertStage stage;
  auto source = std::make_shared<MockVideoFieldRepresentation>();

  auto result = stage.process(source);

  ASSERT_NE(result, nullptr);
  EXPECT_NE(result.get(), source.get());
}

TEST(FieldInvertStageTest, executeThrowsWhenInputMissing) {
  orc::FieldInvertStage stage;
  orc::ObservationContext observation_context;

  EXPECT_THROW(stage.execute({}, {}, observation_context),
               orc::DAGExecutionError);
}
}  // namespace orc_unit_test