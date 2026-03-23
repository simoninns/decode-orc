/*
 * File:        chroma_sink_tweak_panel_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for ChromaSinkStage live preview tweak parameter exposure
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <algorithm>
#include <string>

#include "../../include/video_field_representation_mock.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/core/stages/chroma_sink/chroma_sink_stage.h"

namespace orc_unit_test
{
    using testing::NiceMock;
    using testing::Return;

    namespace
    {
        orc::SourceParameters make_video_params(orc::VideoSystem system)
        {
            orc::SourceParameters params;
            params.system = system;
            params.field_width = 1135;
            params.field_height = 312;
            params.active_video_start = 0;
            params.active_video_end = 702;
            params.first_active_frame_line = 0;
            params.last_active_frame_line = (system == orc::VideoSystem::NTSC) ? 486 : 576;
            params.colour_burst_start = 20;
            params.colour_burst_end = 60;
            params.black_16b_ire = 0;
            params.white_16b_ire = 65535;
            params.sample_rate = 4.0;
            params.fsc = 1.0;
            return params;
        }

        orc::StagePreviewCapability capability_for_system(orc::VideoSystem system)
        {
            orc::ChromaSinkStage stage;
            orc::ObservationContext observation_context;
            auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

            EXPECT_CALL(*vfr, get_video_parameters())
                .WillRepeatedly(Return(make_video_params(system)));
            EXPECT_CALL(*vfr, field_count())
                .WillRepeatedly(Return(200u));

            stage.execute({vfr}, {}, observation_context);
            return stage.get_preview_capability();
        }

        bool has_tweak(
            const orc::StagePreviewCapability& capability,
            const std::string& parameter_name,
            orc::PreviewTweakClass tweak_class)
        {
            return std::any_of(
                capability.tweakable_parameters.begin(),
                capability.tweakable_parameters.end(),
                [&](const orc::PreviewTweakableParameter& tweak) {
                    return tweak.parameter_name == parameter_name && tweak.tweak_class == tweak_class;
                });
        }
    }

    TEST(ChromaSinkTweakPanelTest, previewCapability_listsNtscDecodeTweaks)
    {
        const auto capability = capability_for_system(orc::VideoSystem::NTSC);

        EXPECT_TRUE(has_tweak(capability, "decoder_type", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "chroma_gain", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "chroma_phase", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "ntsc_phase_comp", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "chroma_weight", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "adapt_threshold", orc::PreviewTweakClass::DecodePhase));

        EXPECT_FALSE(has_tweak(capability, "simple_pal", orc::PreviewTweakClass::DecodePhase));
        EXPECT_FALSE(has_tweak(capability, "transform_threshold", orc::PreviewTweakClass::DecodePhase));
    }

    TEST(ChromaSinkTweakPanelTest, previewCapability_listsPalDecodeTweaks)
    {
        const auto capability = capability_for_system(orc::VideoSystem::PAL);

        EXPECT_TRUE(has_tweak(capability, "decoder_type", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "chroma_gain", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "chroma_phase", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "simple_pal", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "transform_threshold", orc::PreviewTweakClass::DecodePhase));

        EXPECT_FALSE(has_tweak(capability, "ntsc_phase_comp", orc::PreviewTweakClass::DecodePhase));
        EXPECT_FALSE(has_tweak(capability, "chroma_weight", orc::PreviewTweakClass::DecodePhase));
        EXPECT_FALSE(has_tweak(capability, "adapt_threshold", orc::PreviewTweakClass::DecodePhase));
    }

    TEST(ChromaSinkTweakPanelTest, previewCapability_excludesOutputAndFileParameters)
    {
        const auto capability = capability_for_system(orc::VideoSystem::NTSC);

        EXPECT_FALSE(has_tweak(capability, "output_path", orc::PreviewTweakClass::DecodePhase));
        EXPECT_FALSE(has_tweak(capability, "output_format", orc::PreviewTweakClass::DecodePhase));
        EXPECT_FALSE(has_tweak(capability, "encoder_preset", orc::PreviewTweakClass::DecodePhase));
        EXPECT_FALSE(has_tweak(capability, "encoder_crf", orc::PreviewTweakClass::DecodePhase));
        EXPECT_FALSE(has_tweak(capability, "encoder_bitrate", orc::PreviewTweakClass::DecodePhase));
    }

    TEST(ChromaSinkTweakPanelTest, setParameters_updatesRuntimeValuesForTweakables)
    {
        orc::ChromaSinkStage stage;
        stage.set_parameters({
            {"decoder_type", std::string("mono")},
            {"chroma_gain", 2.5},
            {"chroma_phase", -12.0},
            {"ntsc_phase_comp", false}
        });

        const auto params = stage.get_parameters();

        auto decoder_it = params.find("decoder_type");
        ASSERT_NE(decoder_it, params.end());
        ASSERT_TRUE(std::holds_alternative<std::string>(decoder_it->second));
        EXPECT_EQ(std::get<std::string>(decoder_it->second), "mono");

        auto gain_it = params.find("chroma_gain");
        ASSERT_NE(gain_it, params.end());
        ASSERT_TRUE(std::holds_alternative<double>(gain_it->second));
        EXPECT_DOUBLE_EQ(std::get<double>(gain_it->second), 2.5);

        auto phase_it = params.find("chroma_phase");
        ASSERT_NE(phase_it, params.end());
        ASSERT_TRUE(std::holds_alternative<double>(phase_it->second));
        EXPECT_DOUBLE_EQ(std::get<double>(phase_it->second), -12.0);

        auto ntsc_phase_it = params.find("ntsc_phase_comp");
        ASSERT_NE(ntsc_phase_it, params.end());
        ASSERT_TRUE(std::holds_alternative<bool>(ntsc_phase_it->second));
        EXPECT_FALSE(std::get<bool>(ntsc_phase_it->second));
    }
}