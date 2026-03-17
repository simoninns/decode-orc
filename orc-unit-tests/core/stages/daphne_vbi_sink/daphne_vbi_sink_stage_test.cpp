/*
* File:        daphne_vbi_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for Daphne VBI sink stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#include "../../factories_interface_mock.h"
#include "../../include/video_field_representation_mock.h"
#include "../../include/observation_context_interface_mock.h"
#include "../stage_factories_interface_mock.h"
#include <gtest/gtest.h>
#include "daphne_vbi_sink_stage.h"
#include "daphne_vbi_sink_stage_deps_interface_mock.h"

using testing::_;
using testing::Return;
using testing::StrictMock;
using testing::Ref;

// using different namespace from module-under-test so that we can use the same class names in the tests as in the module-under-test
namespace orc_unit_test
{
    // test fixture for DaphneVBISinkStage suite of tests
    class DaphneVBISinkStage : public ::testing::Test
    {
    public:
        void SetUp() override
        {
            pMockDeps_ = std::make_shared<StrictMock<MockDaphneVBISinkStageDeps>>();
            pMockRepresentation_ = std::make_shared<StrictMock<MockVideoFieldRepresentation>>();
            pMockFactories_ = std::make_shared<MockFactories>();

            EXPECT_CALL(*pMockFactories_, get_instance_stage_factories())
                .WillRepeatedly(Return(&mockStageFactories_));

            instance_ = std::make_unique<orc::DaphneVBISinkStage>(pMockFactories_);
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
        std::shared_ptr<StrictMock<MockDaphneVBISinkStageDeps>> pMockDeps_;
        std::shared_ptr<StrictMock<MockVideoFieldRepresentation>> pMockRepresentation_;
        MockObservationContext mockObservationContext;

        std::unique_ptr<orc::DaphneVBISinkStage> instance_;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////

    TEST_F(DaphneVBISinkStage, trigger_returns_false_when_output_path_missing)
    {
        const bool result = instance_->trigger({}, {}, mockObservationContext);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: No output path specified");
    }

    TEST_F(DaphneVBISinkStage, trigger_returns_false_when_output_path_is_empty)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("")}
        };

        const bool result = instance_->trigger({}, parameters, mockObservationContext);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: Output path is empty");
    }

    TEST_F(DaphneVBISinkStage, trigger_returns_false_when_no_input_connected)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("out")}
        };

        const bool result = instance_->trigger({}, parameters, mockObservationContext);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: No input connected");
    }

    TEST_F(DaphneVBISinkStage, trigger_returns_false_when_input_not_video_field_representation)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("out")}
        };
        const std::vector<orc::ArtifactPtr> inputs = {nullptr};

        const bool result = instance_->trigger(inputs, parameters, mockObservationContext);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: Input is not a video field representation");
    }

    TEST_F(DaphneVBISinkStage, trigger_writes_vbi_and_sets_success_status)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("out_path")}
        };
        const std::vector<orc::ArtifactPtr> inputs = make_valid_input();

        EXPECT_CALL(mockStageFactories_, CreateInstanceDaphneVBISinkStageDeps(_, _, _))
            .Times(1)
            .WillOnce(Return(pMockDeps_));

        // Ref needed here so gMock doesn't try to do the wrong thing
        EXPECT_CALL(*pMockDeps_, write_vbi(pMockRepresentation_.get(), "out_path", testing::Ref(mockObservationContext)))
                .Times(1)
            .WillOnce(Return(true));

        EXPECT_CALL(*pMockRepresentation_, field_range())
            .Times(1)
            .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(3))));

        const bool result = instance_->trigger(inputs, parameters, mockObservationContext);

        EXPECT_TRUE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Exported 3 fields to out_path");
        EXPECT_FALSE(instance_->is_trigger_in_progress());
    }

    TEST_F(DaphneVBISinkStage, trigger_sets_error_status_when_dep_write_fails)
    {
        const std::map<std::string, orc::ParameterValue> parameters = {
            {"output_path", std::string("out_path")}
        };
        const std::vector<orc::ArtifactPtr> inputs = make_valid_input();

        EXPECT_CALL(mockStageFactories_, CreateInstanceDaphneVBISinkStageDeps(_, _, _))
            .Times(1)
            .WillOnce(Return(pMockDeps_));

        // Ref needed here so gMock doesn't try to do the wrong thing
        EXPECT_CALL(*pMockDeps_, write_vbi(pMockRepresentation_.get(), "out_path", Ref(mockObservationContext)))
            .Times(1)
            .WillOnce(Return(false));

        EXPECT_CALL(*pMockRepresentation_, field_range())
            .Times(0);

        const bool result = instance_->trigger(inputs, parameters, mockObservationContext);

        EXPECT_FALSE(result);
        EXPECT_EQ(instance_->get_trigger_status(), "Error: Failed to write output files");
        EXPECT_FALSE(instance_->is_trigger_in_progress());
    }

    TEST_F(DaphneVBISinkStage, set_parameters_accepts_output_path_string)
    {
        const bool result = instance_->set_parameters({{"output_path", std::string("abc.vbi")}});
        const auto params = instance_->get_parameters();

        EXPECT_TRUE(result);
        ASSERT_TRUE(params.find("output_path") != params.end());
        EXPECT_EQ(std::get<std::string>(params.at("output_path")), "abc.vbi");
    }

    TEST_F(DaphneVBISinkStage, set_parameters_rejects_non_string_output_path)
    {
        const bool result = instance_->set_parameters({{"output_path", int32_t(7)}});

        EXPECT_FALSE(result);
    }
}