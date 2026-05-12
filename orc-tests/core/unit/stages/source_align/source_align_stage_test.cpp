/*
 * File:        source_align_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for SourceAlignStage defaults and validation paths
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include "../../include/video_field_representation_mock.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/plugins/stages/source_align/source_align_stage.h"

namespace orc_unit_test
{
    namespace
    {
        const orc::ParameterDescriptor* find_descriptor(
            const std::vector<orc::ParameterDescriptor>& descriptors,
            const std::string& name)
        {
            auto it = std::find_if(descriptors.begin(), descriptors.end(), [&](const orc::ParameterDescriptor& descriptor) {
                return descriptor.name == name;
            });

            return it == descriptors.end() ? nullptr : &(*it);
        }
    }

    TEST(SourceAlignStageTest, requiredInputCount_isOne)
    {
        orc::SourceAlignStage stage;
        EXPECT_EQ(stage.required_input_count(), 1u);
    }

    TEST(SourceAlignStageTest, outputCount_isUnboundedSentinel)
    {
        orc::SourceAlignStage stage;
        EXPECT_EQ(stage.output_count(), static_cast<size_t>(UINT32_MAX));
    }

    TEST(SourceAlignStageTest, nodeTypeInfo_hasExpectedMetadata)
    {
        orc::SourceAlignStage stage;
        auto info = stage.get_node_type_info();

        EXPECT_EQ(info.type, orc::NodeType::COMPLEX);
        EXPECT_EQ(info.stage_name, "source_align");
        EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
    }

    TEST(SourceAlignStageTest, descriptorDefaults_matchRuntimeDefaults)
    {
        orc::SourceAlignStage stage;
        const auto descriptors = stage.get_parameter_descriptors();
        const auto params = stage.get_parameters();

        const auto* alignment_map = find_descriptor(descriptors, "alignmentMap");
        const auto* enforce_field_order = find_descriptor(descriptors, "enforceFieldOrder");

        ASSERT_NE(alignment_map, nullptr);
        ASSERT_NE(enforce_field_order, nullptr);
        EXPECT_EQ(std::get<std::string>(*alignment_map->constraints.default_value), std::get<std::string>(params.at("alignmentMap")));
        EXPECT_EQ(std::get<bool>(*enforce_field_order->constraints.default_value), std::get<bool>(params.at("enforceFieldOrder")));
    }

    TEST(SourceAlignStageTest, setParameters_acceptsValidValues)
    {
        orc::SourceAlignStage stage;

        const bool result = stage.set_parameters({
            {"alignmentMap", std::string("1+2,2+4")},
            {"enforceFieldOrder", false}
        });
        const auto params = stage.get_parameters();

        EXPECT_TRUE(result);
        EXPECT_EQ(std::get<std::string>(params.at("alignmentMap")), "1+2,2+4");
        EXPECT_FALSE(std::get<bool>(params.at("enforceFieldOrder")));
    }

    TEST(SourceAlignStageTest, setParameters_rejectsWrongType)
    {
        orc::SourceAlignStage stage;
        EXPECT_FALSE(stage.set_parameters({{"enforceFieldOrder", std::string("false")}}));
    }

    TEST(SourceAlignStageTest, setParameters_rejectsUnknownParameter)
    {
        orc::SourceAlignStage stage;
        EXPECT_FALSE(stage.set_parameters({{"unknown", true}}));
    }

    TEST(SourceAlignStageTest, execute_throwsWhenNoInputsProvided)
    {
        orc::SourceAlignStage stage;
        orc::ObservationContext observation_context;

        EXPECT_THROW(stage.execute({}, {}, observation_context), orc::DAGExecutionError);
    }

    TEST(SourceAlignStageTest, execute_throwsWhenTooManyInputsProvided)
    {
        orc::SourceAlignStage stage;
        orc::ObservationContext observation_context;
        std::vector<orc::ArtifactPtr> inputs(17);

        EXPECT_THROW(stage.execute(inputs, {}, observation_context), orc::DAGExecutionError);
    }

    TEST(SourceAlignStageTest, execute_throwsWhenManualAlignmentMapIsInvalid)
    {
        orc::SourceAlignStage stage;
        orc::ObservationContext observation_context;
        auto source = std::make_shared<MockVideoFieldRepresentation>();
        std::vector<orc::ArtifactPtr> inputs = {
            std::static_pointer_cast<orc::Artifact>(source)
        };

        EXPECT_THROW(
            stage.execute(inputs, {{"alignmentMap", std::string("invalid")}}, observation_context),
            orc::DAGExecutionError);
    }
} // namespace orc_unit_test