/*
 * File:        mask_line_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for MaskLineStage defaults and lightweight behavior
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "../../include/video_field_representation_mock.h"
#include "../../../../orc/core/stages/mask_line/mask_line_stage.h"

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

    TEST(MaskLineStageTest, requiredInputCount_isOne)
    {
        orc::MaskLineStage stage;
        EXPECT_EQ(stage.required_input_count(), 1u);
    }

    TEST(MaskLineStageTest, outputCount_isOne)
    {
        orc::MaskLineStage stage;
        EXPECT_EQ(stage.output_count(), 1u);
    }

    TEST(MaskLineStageTest, nodeTypeInfo_hasExpectedMetadata)
    {
        orc::MaskLineStage stage;
        auto info = stage.get_node_type_info();

        EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
        EXPECT_EQ(info.stage_name, "mask_line");
        EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
    }

    TEST(MaskLineStageTest, descriptorDefaults_matchRuntimeDefaults)
    {
        orc::MaskLineStage stage;
        const auto descriptors = stage.get_parameter_descriptors();
        const auto params = stage.get_parameters();

        const auto* line_spec = find_descriptor(descriptors, "lineSpec");
        const auto* mask_ire = find_descriptor(descriptors, "maskIRE");

        ASSERT_NE(line_spec, nullptr);
        ASSERT_NE(mask_ire, nullptr);
        EXPECT_EQ(std::get<std::string>(*line_spec->constraints.default_value), std::get<std::string>(params.at("lineSpec")));
        EXPECT_EQ(std::get<double>(*mask_ire->constraints.default_value), std::get<double>(params.at("maskIRE")));
    }

    TEST(MaskLineStageTest, setParameters_acceptsValidValues)
    {
        orc::MaskLineStage stage;

        const bool result = stage.set_parameters({
            {"lineSpec", std::string("F:20,A:10")},
            {"maskIRE", 50.0}
        });
        const auto params = stage.get_parameters();

        EXPECT_TRUE(result);
        EXPECT_EQ(std::get<std::string>(params.at("lineSpec")), "F:20,A:10");
        EXPECT_EQ(std::get<double>(params.at("maskIRE")), 50.0);
    }

    TEST(MaskLineStageTest, setParameters_ignoresUnknownOrWrongTypes)
    {
        orc::MaskLineStage stage;

        const bool result = stage.set_parameters({
            {"lineSpec", 7.0},
            {"maskIRE", std::string("bad")},
            {"unknown", true}
        });
        const auto params = stage.get_parameters();

        EXPECT_TRUE(result);
        EXPECT_EQ(std::get<std::string>(params.at("lineSpec")), "");
        EXPECT_EQ(std::get<double>(params.at("maskIRE")), 0.0);
    }

    TEST(MaskLineStageTest, process_returnsSourceWhenNoLinesConfigured)
    {
        orc::MaskLineStage stage;
        auto source = std::make_shared<MockVideoFieldRepresentation>();

        const auto result = stage.process(source);

        EXPECT_EQ(result.get(), source.get());
    }

    TEST(MaskLineStageTest, process_wrapsSourceWhenMaskingConfigured)
    {
        orc::MaskLineStage stage;
        auto source = std::make_shared<MockVideoFieldRepresentation>();

        ASSERT_TRUE(stage.set_parameters({{"lineSpec", std::string("F:20")}, {"maskIRE", 0.0}}));
        const auto result = stage.process(source);

        ASSERT_NE(result, nullptr);
        EXPECT_NE(result.get(), source.get());
    }

    TEST(MaskLineStageTest, maskedRepresentation_handlesManyFieldIds)
    {
        orc::MaskLineStage stage;
        auto source = std::make_shared<MockVideoFieldRepresentation>();

        static const std::vector<orc::VideoFieldRepresentation::sample_type> source_line(8, 1234);
        const orc::FieldDescriptor descriptor{
            orc::FieldID(0),
            orc::FieldParity::Top,
            orc::VideoFormat::NTSC,
            orc::VideoSystem::NTSC,
            8,
            262,
            std::nullopt,
            std::nullopt
        };

        EXPECT_CALL(*source, get_line(testing::_, 0))
            .Times(1000)
            .WillRepeatedly(testing::Return(source_line.data()));
        EXPECT_CALL(*source, get_descriptor(testing::_))
            .Times(1000)
            .WillRepeatedly(testing::Return(descriptor));

        ASSERT_TRUE(stage.set_parameters({{"lineSpec", std::string("A:0")}, {"maskIRE", 0.0}}));
        const auto masked = stage.process(source);

        ASSERT_NE(masked, nullptr);
        for (uint64_t i = 0; i < 1000; ++i) {
            const auto* line = masked->get_line(orc::FieldID(i), 0);
            ASSERT_NE(line, nullptr);
            EXPECT_EQ(line[0], 0);
            EXPECT_EQ(line[7], 0);
        }
    }
} // namespace orc_unit_test