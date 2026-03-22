/*
 * File:        video_params_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for VideoParamsStage defaults, validation, and overrides
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include <gmock/gmock.h>

#include "../../include/video_field_representation_mock.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/core/stages/video_params/video_params_stage.h"

using testing::Return;

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

    TEST(VideoParamsStageTest, requiredInputCount_isOne)
    {
        orc::VideoParamsStage stage;
        EXPECT_EQ(stage.required_input_count(), 1u);
    }

    TEST(VideoParamsStageTest, outputCount_isOne)
    {
        orc::VideoParamsStage stage;
        EXPECT_EQ(stage.output_count(), 1u);
    }

    TEST(VideoParamsStageTest, nodeTypeInfo_hasExpectedMetadata)
    {
        orc::VideoParamsStage stage;
        auto info = stage.get_node_type_info();

        EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
        EXPECT_EQ(info.stage_name, "video_params");
        EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
    }

    TEST(VideoParamsStageTest, descriptorDefaults_matchRuntimeDefaults)
    {
        orc::VideoParamsStage stage;
        const auto descriptors = stage.get_parameter_descriptors();
        const auto params = stage.get_parameters();
        const std::array<const char*, 8> parameter_names = {
            "colourBurstStart",
            "colourBurstEnd",
            "activeVideoStart",
            "activeVideoEnd",
            "firstActiveFieldLine",
            "lastActiveFieldLine",
            "white16bIRE",
            "black16bIRE"
        };

        for (const auto* name : parameter_names) {
            const auto* descriptor = find_descriptor(descriptors, name);
            ASSERT_NE(descriptor, nullptr);
            ASSERT_TRUE(descriptor->constraints.default_value.has_value());
            ASSERT_TRUE(std::holds_alternative<int32_t>(*descriptor->constraints.default_value));
            ASSERT_TRUE(std::holds_alternative<int32_t>(params.at(name)));
            EXPECT_EQ(std::get<int32_t>(*descriptor->constraints.default_value), std::get<int32_t>(params.at(name)));
        }
    }

    TEST(VideoParamsStageTest, setParameters_acceptsInt32Overrides)
    {
        orc::VideoParamsStage stage;

        const bool result = stage.set_parameters({
            {"activeVideoStart", int32_t(120)},
            {"black16bIRE", int32_t(1200)}
        });
        const auto params = stage.get_parameters();

        EXPECT_TRUE(result);
        EXPECT_EQ(std::get<int32_t>(params.at("activeVideoStart")), 120);
        EXPECT_EQ(std::get<int32_t>(params.at("black16bIRE")), 1200);
    }

    TEST(VideoParamsStageTest, setParameters_rejectsUnknownParameter)
    {
        orc::VideoParamsStage stage;
        EXPECT_FALSE(stage.set_parameters({{"unknown", int32_t(1)}}));
    }

    TEST(VideoParamsStageTest, setParameters_rejectsWrongType)
    {
        orc::VideoParamsStage stage;
        EXPECT_FALSE(stage.set_parameters({{"activeVideoStart", std::string("120")}}));
    }

    TEST(VideoParamsStageTest, process_appliesConfiguredOverrides)
    {
        orc::VideoParamsStage stage;
        auto source = std::make_shared<testing::NiceMock<MockVideoFieldRepresentation>>();
        orc::SourceParameters source_params;
        source_params.system = orc::VideoSystem::NTSC;
        source_params.field_width = 844;
        source_params.field_height = 263;
        source_params.black_16b_ire = 1000;
        source_params.white_16b_ire = 50000;

        EXPECT_CALL(*source, get_video_parameters()).WillRepeatedly(Return(source_params));
        ASSERT_TRUE(stage.set_parameters({{"activeVideoStart", int32_t(120)}, {"black16bIRE", int32_t(1500)}}));

        const auto result = stage.process(source);

        ASSERT_NE(result, nullptr);
        const auto overridden = result->get_video_parameters();
        ASSERT_TRUE(overridden.has_value());
        EXPECT_EQ(overridden->field_width, 844);
        EXPECT_EQ(overridden->field_height, 263);
        EXPECT_EQ(overridden->active_video_start, 120);
        EXPECT_EQ(overridden->black_16b_ire, 1500);
        EXPECT_EQ(overridden->white_16b_ire, 50000);
    }

    TEST(VideoParamsStageTest, execute_throwsWhenInputMissing)
    {
        orc::VideoParamsStage stage;
        orc::ObservationContext observation_context;

        EXPECT_THROW(stage.execute({}, {}, observation_context), orc::DAGExecutionError);
    }
} // namespace orc_unit_test