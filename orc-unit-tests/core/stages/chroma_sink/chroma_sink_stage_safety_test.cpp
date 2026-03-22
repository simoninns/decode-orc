/*
 * File:        chroma_sink_stage_safety_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for chroma sink invalid metadata hardening
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../../include/video_field_representation_mock.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../../../orc/core/stages/chroma_sink/raw_video_sink_stage.h"
#include "../../../../orc/core/stages/chroma_sink/ffmpeg_video_sink_stage.h"

namespace orc_unit_test
{
    using testing::HasSubstr;
    using testing::NiceMock;
    using testing::Return;

    namespace
    {
        orc::SourceParameters make_too_narrow_ntsc_params()
        {
            orc::SourceParameters params;
            params.system = orc::VideoSystem::NTSC;
            params.field_width = 8;
            params.field_height = 4;
            params.active_video_start = 0;
            params.active_video_end = 8;
            params.first_active_frame_line = 0;
            params.last_active_frame_line = 6;
            params.colour_burst_start = 0;
            params.colour_burst_end = 2;
            params.black_16b_ire = 0;
            params.white_16b_ire = 100;
            params.sample_rate = 4.0;
            params.fsc = 1.0;
            return params;
        }

        orc::SourceParameters make_too_narrow_pal_params()
        {
            auto params = make_too_narrow_ntsc_params();
            params.system = orc::VideoSystem::PAL;
            return params;
        }
    }

    TEST(ChromaSinkStageSafetyTest, rawSinkTriggerRejectsInvalidNtscGeometry)
    {
        orc::RawVideoSinkStage stage;
        MockObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*vfr, get_video_parameters()).WillRepeatedly(Return(make_too_narrow_ntsc_params()));
        EXPECT_CALL(*vfr, get_active_line_hint()).WillRepeatedly(Return(std::nullopt));

        const bool result = stage.trigger(
            {vfr},
            {{"output_path", std::string("ignored.y4m")}, {"decoder_type", std::string("ntsc2d")}, {"output_format", std::string("y4m")}},
            observation_context);

        EXPECT_FALSE(result);
        EXPECT_THAT(stage.get_trigger_status(), HasSubstr("Invalid video parameters"));
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    TEST(ChromaSinkStageSafetyTest, ffmpegSinkTriggerRejectsInvalidPalGeometry)
    {
        orc::FFmpegVideoSinkStage stage;
        MockObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        EXPECT_CALL(*vfr, get_video_parameters()).WillRepeatedly(Return(make_too_narrow_pal_params()));
        EXPECT_CALL(*vfr, get_active_line_hint()).WillRepeatedly(Return(std::nullopt));

        const bool result = stage.trigger(
            {vfr},
            {{"output_path", std::string("ignored.mp4")}, {"decoder_type", std::string("pal2d")}, {"output_format", std::string("mp4-h264")}},
            observation_context);

        EXPECT_FALSE(result);
        EXPECT_THAT(stage.get_trigger_status(), HasSubstr("Invalid video parameters"));
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }
}