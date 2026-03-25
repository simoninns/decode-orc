/*
 * File:        pal_comp_source_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for PALCompSourceStage parameter descriptors, defaults,
 *              set_parameters validation, and stage interface invariants.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <algorithm>

#include "../../include/video_field_representation_mock.h"
#include "../../../../orc/core/stages/pal_comp_source/pal_comp_source_stage.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../../../../orc/common/include/error_types.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace orc_unit_test
{
    class MockPALCompSourceLoader : public orc::IPALCompSourceLoader
    {
    public:
        MOCK_METHOD(
            std::shared_ptr<orc::VideoFieldRepresentation>,
            load,
            (const std::string& input_path,
             const std::string& db_path,
             const std::string& pcm_path,
             const std::string& efm_path),
            (const, override));
    };

    orc::SourceParameters make_pal_family_comp_source_parameters(orc::VideoSystem system)
    {
        orc::SourceParameters params;
        params.system = system;
        params.decoder = "ld-decode";
        params.field_width = 909;
        params.field_height = (system == orc::VideoSystem::PAL) ? 313 : 263;
        params.number_of_sequential_fields = 1449;
        return params;
    }

    // =========================================================================
    // Stage interface invariants
    // =========================================================================

    TEST(PALCompSourceStageTest, requiredInputCount_isZero)
    {
        orc::PALCompSourceStage stage;
        EXPECT_EQ(stage.required_input_count(), 0u);
    }

    TEST(PALCompSourceStageTest, outputCount_isOne)
    {
        orc::PALCompSourceStage stage;
        EXPECT_EQ(stage.output_count(), 1u);
    }

    TEST(PALCompSourceStageTest, nodeTypeInfo_hasSourceType)
    {
        orc::PALCompSourceStage stage;
        auto info = stage.get_node_type_info();
        EXPECT_EQ(info.type, orc::NodeType::SOURCE);
    }

    TEST(PALCompSourceStageTest, nodeTypeInfo_hasExpectedStageName)
    {
        orc::PALCompSourceStage stage;
        auto info = stage.get_node_type_info();
        EXPECT_EQ(info.stage_name, "PAL_Comp_Source");
    }

    TEST(PALCompSourceStageTest, nodeTypeInfo_formatCompatibilityIsPalOnly)
    {
        orc::PALCompSourceStage stage;
        auto info = stage.get_node_type_info();
        EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::PAL_ONLY);
    }

    // =========================================================================
    // Parameter descriptor tests
    // =========================================================================

    TEST(PALCompSourceStageTest, parameterDescriptors_containsInputPath)
    {
        orc::PALCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();

        auto it = std::find_if(descriptors.begin(), descriptors.end(),
            [](const orc::ParameterDescriptor& d) { return d.name == "input_path"; });

        ASSERT_NE(it, descriptors.end());
        EXPECT_EQ(it->type, orc::ParameterType::FILE_PATH);
        EXPECT_EQ(it->file_extension_hint, ".tbc");
    }

    TEST(PALCompSourceStageTest, parameterDescriptors_containsPcmPath)
    {
        orc::PALCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();

        auto it = std::find_if(descriptors.begin(), descriptors.end(),
            [](const orc::ParameterDescriptor& d) { return d.name == "pcm_path"; });

        ASSERT_NE(it, descriptors.end());
        EXPECT_EQ(it->type, orc::ParameterType::FILE_PATH);
        EXPECT_EQ(it->file_extension_hint, ".pcm");
    }

    TEST(PALCompSourceStageTest, parameterDescriptors_containsEfmPath)
    {
        orc::PALCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();

        auto it = std::find_if(descriptors.begin(), descriptors.end(),
            [](const orc::ParameterDescriptor& d) { return d.name == "efm_path"; });

        ASSERT_NE(it, descriptors.end());
        EXPECT_EQ(it->type, orc::ParameterType::FILE_PATH);
        EXPECT_EQ(it->file_extension_hint, ".efm");
    }

    TEST(PALCompSourceStageTest, descriptorDefaults_inputPath_isEmptyString)
    {
        orc::PALCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();

        auto it = std::find_if(descriptors.begin(), descriptors.end(),
            [](const orc::ParameterDescriptor& d) { return d.name == "input_path"; });

        ASSERT_NE(it, descriptors.end());
        ASSERT_TRUE(it->constraints.default_value.has_value());
        ASSERT_TRUE(std::holds_alternative<std::string>(*it->constraints.default_value));
        EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), "");
    }

    TEST(PALCompSourceStageTest, descriptorDefaults_pcmPath_isEmptyString)
    {
        orc::PALCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();

        auto it = std::find_if(descriptors.begin(), descriptors.end(),
            [](const orc::ParameterDescriptor& d) { return d.name == "pcm_path"; });

        ASSERT_NE(it, descriptors.end());
        ASSERT_TRUE(it->constraints.default_value.has_value());
        ASSERT_TRUE(std::holds_alternative<std::string>(*it->constraints.default_value));
        EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), "");
    }

    TEST(PALCompSourceStageTest, descriptorDefaults_efmPath_isEmptyString)
    {
        orc::PALCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();

        auto it = std::find_if(descriptors.begin(), descriptors.end(),
            [](const orc::ParameterDescriptor& d) { return d.name == "efm_path"; });

        ASSERT_NE(it, descriptors.end());
        ASSERT_TRUE(it->constraints.default_value.has_value());
        ASSERT_TRUE(std::holds_alternative<std::string>(*it->constraints.default_value));
        EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), "");
    }

    TEST(PALCompSourceStageTest, parameterDescriptors_allParametersAreOptional)
    {
        orc::PALCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();

        for (const auto& desc : descriptors) {
            EXPECT_FALSE(desc.constraints.required)
                << "Parameter '" << desc.name << "' should be optional";
        }
    }

    // =========================================================================
    // set_parameters validation tests
    // =========================================================================

    TEST(PALCompSourceStageTest, setParameters_acceptsValidStringMap)
    {
        orc::PALCompSourceStage stage;
        const std::map<std::string, orc::ParameterValue> params = {
            {"input_path", std::string("/some/file.tbc")},
            {"pcm_path",   std::string("")},
            {"efm_path",   std::string("")}
        };

        EXPECT_TRUE(stage.set_parameters(params));
    }

    TEST(PALCompSourceStageTest, setParameters_rejectsNonStringInputPath)
    {
        orc::PALCompSourceStage stage;
        const std::map<std::string, orc::ParameterValue> params = {
            {"input_path", int32_t(42)}
        };

        EXPECT_FALSE(stage.set_parameters(params));
    }

    TEST(PALCompSourceStageTest, setParameters_acceptsEmptyMap)
    {
        orc::PALCompSourceStage stage;
        EXPECT_TRUE(stage.set_parameters({}));
    }

    // =========================================================================
    // execute() contract tests
    // =========================================================================

    TEST(PALCompSourceStageTest, execute_throwsWhenInputProvided)
    {
        orc::PALCompSourceStage stage;
        orc::ObservationContext observation_context;

        EXPECT_THROW(
            stage.execute({nullptr}, {}, observation_context),
            std::runtime_error);
    }

    TEST(PALCompSourceStageTest, execute_returnsEmptyWhenInputPathMissing)
    {
        orc::PALCompSourceStage stage;
        orc::ObservationContext observation_context;

        const auto outputs = stage.execute({}, {}, observation_context);

        EXPECT_TRUE(outputs.empty());
    }

    TEST(PALCompSourceStageTest, execute_returnsEmptyWhenInputPathEmpty)
    {
        orc::PALCompSourceStage stage;
        orc::ObservationContext observation_context;

        const auto outputs = stage.execute({}, {{"input_path", std::string("")}}, observation_context);

        EXPECT_TRUE(outputs.empty());
    }

    TEST(PALCompSourceStageTest, execute_loadsPalMRepresentationThroughInjectedLoader)
    {
        auto loader = std::make_shared<StrictMock<MockPALCompSourceLoader>>();
        auto representation = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
        orc::ObservationContext observation_context;
        orc::PALCompSourceStage stage(loader);

        EXPECT_CALL(*representation, get_video_parameters())
            .Times(1)
            .WillOnce(Return(make_pal_family_comp_source_parameters(orc::VideoSystem::PAL_M)));

        EXPECT_CALL(*loader, load("/tmp/source.tbc", "/tmp/source.tbc.db", "", ""))
            .Times(1)
            .WillOnce(Return(std::static_pointer_cast<orc::VideoFieldRepresentation>(representation)));

        const auto outputs = stage.execute(
            {},
            {{"input_path", std::string("/tmp/source.tbc")}},
            observation_context);

        ASSERT_EQ(outputs.size(), 1u);
        EXPECT_EQ(outputs.front(), std::static_pointer_cast<orc::Artifact>(representation));
        EXPECT_TRUE(stage.supports_preview());
    }

    TEST(PALCompSourceStageTest, execute_throwsWhenLoadedMetadataIsNotPalFamily)
    {
        auto loader = std::make_shared<StrictMock<MockPALCompSourceLoader>>();
        auto representation = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
        orc::ObservationContext observation_context;
        orc::PALCompSourceStage stage(loader);

        EXPECT_CALL(*representation, get_video_parameters())
            .Times(1)
            .WillOnce(Return(make_pal_family_comp_source_parameters(orc::VideoSystem::NTSC)));

        EXPECT_CALL(*loader, load(_, _, _, _))
            .Times(1)
            .WillOnce(Return(std::static_pointer_cast<orc::VideoFieldRepresentation>(representation)));

        EXPECT_THROW(
            stage.execute(
                {},
                {{"input_path", std::string("/tmp/source.tbc")}},
                observation_context),
            orc::UserDataError);
    }

    TEST(PALCompSourceStageTest, generateReport_usesInjectedLoaderMetadataForPalM)
    {
        auto loader = std::make_shared<StrictMock<MockPALCompSourceLoader>>();
        auto representation = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
        orc::PALCompSourceStage stage(loader);

        ASSERT_TRUE(stage.set_parameters({
            {"input_path", std::string("/tmp/source.tbc")}
        }));

        EXPECT_CALL(*representation, get_video_parameters())
            .Times(1)
            .WillOnce(Return(make_pal_family_comp_source_parameters(orc::VideoSystem::PAL_M)));
        EXPECT_CALL(*representation, field_range())
            .Times(1)
            .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(2))));
        EXPECT_CALL(*representation, get_audio_sample_count(orc::FieldID(0)))
            .Times(1)
            .WillOnce(Return(100u));
        EXPECT_CALL(*representation, get_audio_sample_count(orc::FieldID(1)))
            .Times(1)
            .WillOnce(Return(200u));
        EXPECT_CALL(*representation, get_efm_sample_count(orc::FieldID(0)))
            .Times(1)
            .WillOnce(Return(10u));
        EXPECT_CALL(*representation, get_efm_sample_count(orc::FieldID(1)))
            .Times(1)
            .WillOnce(Return(20u));
        EXPECT_CALL(*representation, has_audio())
            .Times(1)
            .WillOnce(Return(true));
        EXPECT_CALL(*representation, has_efm())
            .Times(1)
            .WillOnce(Return(true));

        EXPECT_CALL(*loader, load("/tmp/source.tbc", "/tmp/source.tbc.db", "", ""))
            .Times(1)
            .WillOnce(Return(std::static_pointer_cast<orc::VideoFieldRepresentation>(representation)));

        const auto report = stage.generate_report();

        ASSERT_TRUE(report.has_value());
        EXPECT_EQ(report->summary, "PAL Source Status");
        EXPECT_EQ(std::get<int64_t>(report->metrics.at("field_count")), 1449);
        EXPECT_EQ(std::get<int64_t>(report->metrics.at("field_width")), 909);
        EXPECT_EQ(std::get<int64_t>(report->metrics.at("field_height")), 263);
        EXPECT_EQ(std::get<int64_t>(report->metrics.at("audio_samples")), 300);
        EXPECT_EQ(std::get<int64_t>(report->metrics.at("efm_tvalues")), 30);
    }

} // namespace orc_unit_test
