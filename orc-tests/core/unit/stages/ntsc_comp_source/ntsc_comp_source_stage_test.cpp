/*
 * File:        ntsc_comp_source_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for NTSCCompSourceStage parameter descriptors, defaults,
 *              set_parameters validation, and stage interface invariants.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <algorithm>

#include "../../include/video_field_representation_mock.h"
#include "../source_common/source_stage_descriptor_test_utils.h"
#include "../../../../orc/plugins/stages/ntsc_comp_source/ntsc_comp_source_stage.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../../../../orc/common/include/error_types.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace orc_unit_test
{
    class MockNTSCCompSourceLoader : public orc::INTSCCompSourceLoader
    {
    public:
        MOCK_METHOD(
            std::shared_ptr<orc::VideoFieldRepresentation>,
            load,
            (const std::string& input_path,
             const std::string& db_path,
             const std::string& pcm_path,
             const std::string& efm_path,
             const std::string& ac3rf_path),
            (const, override));
    };

    orc::SourceParameters make_ntsc_comp_source_parameters(orc::VideoSystem system)
    {
        orc::SourceParameters params;
        params.system = system;
        params.decoder = "ld-decode";
        params.field_width = 910;
        params.field_height = 263;
        params.number_of_sequential_fields = 1200;
        return params;
    }

    // =========================================================================
    // Parameter descriptor tests
    // =========================================================================

    TEST(NTSCCompSourceStageTest, parameterDescriptors_containsInputPath)
    {
        orc::NTSCCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();
        expect_file_path_descriptor(descriptors, "input_path", ".tbc");
    }

    TEST(NTSCCompSourceStageTest, parameterDescriptors_containsPcmPath)
    {
        orc::NTSCCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();
        expect_file_path_descriptor(descriptors, "pcm_path", ".pcm");
    }

    TEST(NTSCCompSourceStageTest, parameterDescriptors_containsEfmPath)
    {
        orc::NTSCCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();
        expect_file_path_descriptor(descriptors, "efm_path", ".efm");
    }

    TEST(NTSCCompSourceStageTest, descriptorDefaults_inputPath_isEmptyString)
    {
        orc::NTSCCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();
        expect_empty_string_default(descriptors, "input_path");
    }

    TEST(NTSCCompSourceStageTest, descriptorDefaults_pcmPath_isEmptyString)
    {
        orc::NTSCCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();
        expect_empty_string_default(descriptors, "pcm_path");
    }

    TEST(NTSCCompSourceStageTest, descriptorDefaults_efmPath_isEmptyString)
    {
        orc::NTSCCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();
        expect_empty_string_default(descriptors, "efm_path");
    }

    TEST(NTSCCompSourceStageTest, parameterDescriptors_allParametersAreOptional)
    {
        orc::NTSCCompSourceStage stage;
        auto descriptors = stage.get_parameter_descriptors();
        expect_all_descriptors_optional(descriptors);
    }

    // =========================================================================
    // set_parameters validation tests
    // =========================================================================

    TEST(NTSCCompSourceStageTest, setParameters_acceptsValidStringMap)
    {
        orc::NTSCCompSourceStage stage;
        const std::map<std::string, orc::ParameterValue> params = {
            {"input_path", std::string("/some/file.tbc")},
            {"pcm_path",   std::string("")},
            {"efm_path",   std::string("")}
        };

        EXPECT_TRUE(stage.set_parameters(params));
    }

    TEST(NTSCCompSourceStageTest, setParameters_rejectsNonStringInputPath)
    {
        orc::NTSCCompSourceStage stage;
        const std::map<std::string, orc::ParameterValue> params = {
            {"input_path", int32_t(42)}
        };

        EXPECT_FALSE(stage.set_parameters(params));
    }

    TEST(NTSCCompSourceStageTest, setParameters_acceptsEmptyMap)
    {
        orc::NTSCCompSourceStage stage;
        EXPECT_TRUE(stage.set_parameters({}));
    }

    // =========================================================================
    // execute() contract tests
    // =========================================================================

    TEST(NTSCCompSourceStageTest, execute_throwsWhenInputProvided)
    {
        orc::NTSCCompSourceStage stage;
        orc::ObservationContext observation_context;

        EXPECT_THROW(
            stage.execute({nullptr}, {}, observation_context),
            std::runtime_error);
    }

    TEST(NTSCCompSourceStageTest, execute_returnsEmptyWhenInputPathMissing)
    {
        orc::NTSCCompSourceStage stage;
        orc::ObservationContext observation_context;

        const auto outputs = stage.execute({}, {}, observation_context);

        EXPECT_TRUE(outputs.empty());
    }

    TEST(NTSCCompSourceStageTest, execute_returnsEmptyWhenInputPathEmpty)
    {
        orc::NTSCCompSourceStage stage;
        orc::ObservationContext observation_context;

        const auto outputs = stage.execute({}, {{"input_path", std::string("")}}, observation_context);

        EXPECT_TRUE(outputs.empty());
    }

    TEST(NTSCCompSourceStageTest, execute_loadsNtscRepresentationThroughInjectedLoader)
    {
        auto loader = std::make_shared<StrictMock<MockNTSCCompSourceLoader>>();
        auto representation = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
        orc::ObservationContext observation_context;
        orc::NTSCCompSourceStage stage(loader);

        EXPECT_CALL(*representation, get_video_parameters())
            .Times(1)
            .WillOnce(Return(make_ntsc_comp_source_parameters(orc::VideoSystem::NTSC)));

        EXPECT_CALL(*loader, load("/tmp/source.tbc", "/tmp/source.tbc.db", "", "", ""))
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

    TEST(NTSCCompSourceStageTest, execute_throwsWhenLoadedMetadataIsNotNtsc)
    {
        auto loader = std::make_shared<StrictMock<MockNTSCCompSourceLoader>>();
        auto representation = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
        orc::ObservationContext observation_context;
        orc::NTSCCompSourceStage stage(loader);

        EXPECT_CALL(*representation, get_video_parameters())
            .Times(1)
            .WillOnce(Return(make_ntsc_comp_source_parameters(orc::VideoSystem::PAL)));

        EXPECT_CALL(*loader, load(_, _, _, _, _))
            .Times(1)
            .WillOnce(Return(std::static_pointer_cast<orc::VideoFieldRepresentation>(representation)));

        EXPECT_THROW(
            stage.execute(
                {},
                {{"input_path", std::string("/tmp/source.tbc")}},
                observation_context),
            orc::UserDataError);
    }

} // namespace orc_unit_test
