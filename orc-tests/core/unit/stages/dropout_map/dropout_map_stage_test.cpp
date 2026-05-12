/*
 * File:        dropout_map_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for DropoutMapStage defaults and lightweight validation paths
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/plugins/stages/dropout_map/dropout_map_stage.h"

namespace orc_unit_test
{
    namespace
    {
        class FakeArtifact : public orc::Artifact
        {
        public:
            FakeArtifact() : Artifact(orc::ArtifactID("fake"), orc::Provenance{}) {}

            std::string type_name() const override
            {
                return "fake_artifact";
            }
        };
    }

    TEST(DropoutMapStageTest, requiredInputCount_isOne)
    {
        orc::DropoutMapStage stage;
        EXPECT_EQ(stage.required_input_count(), 1u);
    }

    TEST(DropoutMapStageTest, outputCount_isOne)
    {
        orc::DropoutMapStage stage;
        EXPECT_EQ(stage.output_count(), 1u);
    }

    TEST(DropoutMapStageTest, nodeTypeInfo_hasExpectedMetadata)
    {
        orc::DropoutMapStage stage;
        auto info = stage.get_node_type_info();

        EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
        EXPECT_EQ(info.stage_name, "dropout_map");
        EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
    }

    TEST(DropoutMapStageTest, descriptorDefault_matchesRuntimeDefault)
    {
        orc::DropoutMapStage stage;
        const auto descriptors = stage.get_parameter_descriptors();
        const auto params = stage.get_parameters();
        auto it = std::find_if(descriptors.begin(), descriptors.end(), [](const orc::ParameterDescriptor& descriptor) {
            return descriptor.name == "dropout_map";
        });

        ASSERT_NE(it, descriptors.end());
        ASSERT_TRUE(it->constraints.default_value.has_value());
        ASSERT_TRUE(std::holds_alternative<std::string>(*it->constraints.default_value));
        ASSERT_TRUE(std::holds_alternative<std::string>(params.at("dropout_map")));
        EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), std::get<std::string>(params.at("dropout_map")));
    }

    TEST(DropoutMapStageTest, setParameters_acceptsDropoutMapString)
    {
        orc::DropoutMapStage stage;

        const bool result = stage.set_parameters({{
            "dropout_map",
            std::string("[{field:0,add:[{line:10,start:100,end:200}],remove:[]}]")
        }});
        const auto params = stage.get_parameters();

        EXPECT_TRUE(result);
        EXPECT_EQ(std::get<std::string>(params.at("dropout_map")), "[{field:0,add:[{line:10,start:100,end:200}],remove:[]}]");
    }

    TEST(DropoutMapStageTest, execute_throwsWhenInputCountIsNotOne)
    {
        orc::DropoutMapStage stage;
        orc::ObservationContext observation_context;

        EXPECT_THROW(stage.execute({}, {}, observation_context), std::runtime_error);
    }

    TEST(DropoutMapStageTest, execute_throwsWhenInputIsWrongArtifactType)
    {
        orc::DropoutMapStage stage;
        orc::ObservationContext observation_context;
        std::vector<orc::ArtifactPtr> inputs = {std::make_shared<FakeArtifact>()};

        EXPECT_THROW(stage.execute(inputs, {}, observation_context), std::runtime_error);
    }
} // namespace orc_unit_test