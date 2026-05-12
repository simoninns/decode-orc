/*
 * File:        ac3rf_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for AC3RFSinkStage parameter contracts and trigger behaviour
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <algorithm>

#include "../../include/video_field_representation_mock.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../../../orc/plugins/stages/ac3rf_sink/ac3rf_sink_stage.h"
#include "../../../../orc/plugins/stages/ac3rf_sink/ac3rf_sink_stage_deps_interface.h"

namespace orc_unit_test
{
    using testing::NiceMock;
    using testing::Return;
    using testing::StrictMock;

    class MockAC3RFSinkStageDeps : public orc::IAC3RFSinkStageDeps
    {
    public:
        MOCK_METHOD(
            orc::AC3RFSinkDecodeResult,
            decode_and_write_ac3,
            (const orc::VideoFieldRepresentation* representation, const std::string& output_path),
            (override));
    };

    // -------------------------------------------------------------------------
    // Stage interface invariants
    // -------------------------------------------------------------------------

    TEST(AC3RFSinkStageTest, stageInterface_invariantsMatchSink)
    {
        orc::AC3RFSinkStage stage;
        EXPECT_EQ(stage.required_input_count(), 1u);
        EXPECT_EQ(stage.output_count(), 0u);
        EXPECT_EQ(stage.get_node_type_info().type, orc::NodeType::SINK);
    }

    TEST(AC3RFSinkStageTest, stageName_isAC3RFSink)
    {
        orc::AC3RFSinkStage stage;
        EXPECT_EQ(stage.get_node_type_info().stage_name, "AC3RFSink");
    }

    // -------------------------------------------------------------------------
    // Parameter descriptor / default parity
    // -------------------------------------------------------------------------

    TEST(AC3RFSinkStageTest, descriptorDefaults_outputPathIsEmptyAc3)
    {
        orc::AC3RFSinkStage stage;
        const auto descriptors = stage.get_parameter_descriptors();

        auto it = std::find_if(
            descriptors.begin(), descriptors.end(),
            [](const orc::ParameterDescriptor& d) { return d.name == "output_path"; });

        ASSERT_NE(it, descriptors.end());
        EXPECT_EQ(it->type, orc::ParameterType::FILE_PATH);
        EXPECT_EQ(it->file_extension_hint, ".ac3");
        ASSERT_TRUE(it->constraints.default_value.has_value());
        EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), "");
    }

    // -------------------------------------------------------------------------
    // Trigger failure paths (no filesystem / network / clock)
    // -------------------------------------------------------------------------

    TEST(AC3RFSinkStageTest, triggerFails_whenNoInputProvided)
    {
        orc::AC3RFSinkStage stage;
        MockObservationContext observation_context;

        const bool result = stage.trigger({}, {}, observation_context);

        EXPECT_FALSE(result);
        EXPECT_EQ(stage.get_trigger_status(),
                  "Error: AC3 RF sink requires one input (VideoFieldRepresentation)");
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(AC3RFSinkStageTest, triggerFails_whenInputHasNoAC3RFData)
    {
        orc::AC3RFSinkStage stage;
        MockObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*vfr, has_ac3_rf()).WillOnce(Return(false));

        const bool result = stage.trigger(
            {vfr},
            {{"output_path", std::string("ignored.ac3")}},
            observation_context);

        EXPECT_FALSE(result);
        EXPECT_THAT(stage.get_trigger_status(),
                    testing::HasSubstr("does not have AC3 RF symbols data"));
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(AC3RFSinkStageTest, triggerFails_whenOutputPathMissing)
    {
        orc::AC3RFSinkStage stage;
        MockObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*vfr, has_ac3_rf()).WillOnce(Return(true));

        // No "output_path" in parameters
        const bool result = stage.trigger({vfr}, {}, observation_context);

        EXPECT_FALSE(result);
        EXPECT_THAT(stage.get_trigger_status(),
                    testing::HasSubstr("output_path parameter is required"));
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(AC3RFSinkStageTest, triggerUsesDepsSeam_andReportsSuccess)
    {
        orc::AC3RFSinkStage stage;
        auto deps = std::make_shared<StrictMock<MockAC3RFSinkStageDeps>>();
        stage.set_deps_override(deps);

        MockObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*vfr, has_ac3_rf()).WillOnce(Return(true));
        EXPECT_CALL(*deps, decode_and_write_ac3(vfr.get(), "out.ac3"))
            .WillOnce(Return(orc::AC3RFSinkDecodeResult{true, 42, "Success: 42 frames written"}));

        const bool result = stage.trigger({vfr}, {{"output_path", std::string("out.ac3")}}, observation_context);

        EXPECT_TRUE(result);
        EXPECT_EQ(stage.get_trigger_status(), "Success: 42 frames written");
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(AC3RFSinkStageTest, triggerUsesDepsSeam_andPropagatesFailure)
    {
        orc::AC3RFSinkStage stage;
        auto deps = std::make_shared<StrictMock<MockAC3RFSinkStageDeps>>();
        stage.set_deps_override(deps);

        MockObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*vfr, has_ac3_rf()).WillOnce(Return(true));
        EXPECT_CALL(*deps, decode_and_write_ac3(vfr.get(), "out.ac3"))
            .WillOnce(Return(orc::AC3RFSinkDecodeResult{false, 0, "Cancelled by user"}));

        const bool result = stage.trigger({vfr}, {{"output_path", std::string("out.ac3")}}, observation_context);

        EXPECT_FALSE(result);
        EXPECT_EQ(stage.get_trigger_status(), "Cancelled by user");
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

} // namespace orc_unit_test
