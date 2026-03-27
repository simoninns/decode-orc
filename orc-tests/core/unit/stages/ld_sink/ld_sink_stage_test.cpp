/*
 * File:        ld_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for LDSinkStage parameter contracts and trigger validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "../../factories_interface_mock.h"
#include "../../include/video_field_representation_mock.h"
#include "../../include/observation_context_interface_mock.h"
#include "../stage_factories_interface_mock.h"
#include "ld_sink_stage.h"
#include "ld_sink_stage_deps_interface_mock.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::Ref;

namespace orc_unit_test
{
    // test fixture for LDSinkStage suite of tests
    class LDSinkStageTest : public ::testing::Test
    {
    public:
        void SetUp() override
        {
            pMockDeps_ = std::make_shared<StrictMock<MockLDSinkStageDeps>>();
            pMockRepresentation_ = std::make_shared<StrictMock<MockVideoFieldRepresentation>>();
            pMockFactories_ = std::make_shared<MockFactories>();

            EXPECT_CALL(*pMockFactories_, get_instance_stage_factories())
                .WillRepeatedly(ReturnRef(mockStageFactories_));

            instance_ = std::make_unique<orc::LDSinkStage>(pMockFactories_);
        }

        void TearDown() override
        {
            instance_.reset();
        }

    protected:
        std::vector<orc::ArtifactPtr> make_valid_input() const
        {
            return {std::static_pointer_cast<orc::Artifact>(pMockRepresentation_)};
        }

        std::shared_ptr<MockFactories> pMockFactories_;
        StrictMock<MockStageFactories> mockStageFactories_;
        std::shared_ptr<StrictMock<MockLDSinkStageDeps>> pMockDeps_;
        std::shared_ptr<StrictMock<MockVideoFieldRepresentation>> pMockRepresentation_;
        MockObservationContext mockObservationContext_;

        std::unique_ptr<orc::LDSinkStage> instance_;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////

    TEST_F(LDSinkStageTest, descriptorDefaults_outputPathIsEmptyTbc)
    {
        const auto descriptors = instance_->get_parameter_descriptors();

        auto it = std::find_if(descriptors.begin(), descriptors.end(), [](const orc::ParameterDescriptor& d) {
            return d.name == "output_path";
        });

        ASSERT_NE(it, descriptors.end());
        EXPECT_EQ(it->type, orc::ParameterType::FILE_PATH);
        EXPECT_EQ(it->file_extension_hint, ".tbc");
        ASSERT_TRUE(it->constraints.default_value.has_value());
        EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), "");

        const auto params = instance_->get_parameters();
        ASSERT_TRUE(params.find("output_path") != params.end());
        EXPECT_EQ(std::get<std::string>(params.at("output_path")), "");
    }

    TEST_F(LDSinkStageTest, trigger_returns_false_when_output_path_missing)
    {
        const bool result = instance_->trigger({}, {}, mockObservationContext_);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: No output path specified");
    }

    TEST_F(LDSinkStageTest, trigger_returns_false_when_output_path_is_empty)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("")}
        };

        const bool result = instance_->trigger({}, parameters, mockObservationContext_);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: Output path is empty");
    }

    TEST_F(LDSinkStageTest, trigger_returns_false_when_no_input_connected)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("out")}
        };

        const bool result = instance_->trigger({}, parameters, mockObservationContext_);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: No input connected");
    }

    TEST_F(LDSinkStageTest, trigger_returns_false_when_input_not_video_field_representation)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("out")}
        };
        const std::vector<orc::ArtifactPtr> inputs = {nullptr};

        const bool result = instance_->trigger(inputs, parameters, mockObservationContext_);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: Input is not a video field representation");
    }

    TEST_F(LDSinkStageTest, trigger_writes_tbc_and_sets_success_status)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("out_path")}
        };
        const std::vector<orc::ArtifactPtr> inputs = make_valid_input();

        EXPECT_CALL(mockObservationContext_, clear()).Times(1);

        EXPECT_CALL(mockStageFactories_, CreateInstanceLDSinkStageDeps(_, _, _))
            .Times(1)
            .WillOnce(Return(pMockDeps_));

        EXPECT_CALL(*pMockDeps_, write_tbc_and_metadata(pMockRepresentation_.get(), "out_path", Ref(mockObservationContext_)))
            .Times(1)
            .WillOnce(Return(true));

        EXPECT_CALL(*pMockRepresentation_, field_range())
            .Times(1)
            .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(3))));

        const bool result = instance_->trigger(inputs, parameters, mockObservationContext_);

        EXPECT_TRUE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Exported 3 fields to out_path");
        EXPECT_FALSE(instance_->is_trigger_in_progress());
    }

    TEST_F(LDSinkStageTest, trigger_sets_error_status_when_dep_write_fails)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("out_path")}
        };
        const std::vector<orc::ArtifactPtr> inputs = make_valid_input();

        EXPECT_CALL(mockObservationContext_, clear()).Times(1);

        EXPECT_CALL(mockStageFactories_, CreateInstanceLDSinkStageDeps(_, _, _))
            .Times(1)
            .WillOnce(Return(pMockDeps_));

        EXPECT_CALL(*pMockDeps_, write_tbc_and_metadata(pMockRepresentation_.get(), "out_path", Ref(mockObservationContext_)))
            .Times(1)
            .WillOnce(Return(false));

        EXPECT_CALL(*pMockRepresentation_, field_range()).Times(0);

        const bool result = instance_->trigger(inputs, parameters, mockObservationContext_);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: Failed to write output files");
        EXPECT_FALSE(instance_->is_trigger_in_progress());
    }

    TEST_F(LDSinkStageTest, set_parameters_accepts_output_path_string)
    {
        const bool result = instance_->set_parameters({{"output_path", std::string("out.tbc")}});
        const auto params = instance_->get_parameters();

        EXPECT_TRUE(result);
        ASSERT_TRUE(params.find("output_path") != params.end());
        EXPECT_EQ(std::get<std::string>(params.at("output_path")), "out.tbc");
    }

    TEST_F(LDSinkStageTest, set_parameters_rejects_non_string_output_path)
    {
        const bool result = instance_->set_parameters({{"output_path", int32_t(7)}});

        EXPECT_FALSE(result);
    }
}

