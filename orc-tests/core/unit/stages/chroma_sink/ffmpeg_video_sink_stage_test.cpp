/*
 * File:        ffmpeg_video_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for FFmpegVideoSinkStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "../../../../orc/core/stages/chroma_sink/ffmpeg_video_sink_stage.h"

namespace orc_unit_test
{
    namespace
    {
        const orc::ParameterDescriptor* find_parameter(
            const std::vector<orc::ParameterDescriptor>& descriptors,
            const std::string& name)
        {
            auto it = std::find_if(
                descriptors.begin(),
                descriptors.end(),
                [&name](const orc::ParameterDescriptor& descriptor) {
                    return descriptor.name == name;
                });

            return (it == descriptors.end()) ? nullptr : &(*it);
        }

        bool has_string(
            const std::vector<std::string>& values,
            const std::string& value)
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }
    }

    TEST(FFmpegVideoSinkStageTest, parameterDescriptors_filterOutRawOutputFormats)
    {
        orc::FFmpegVideoSinkStage stage;
        auto descriptors = stage.get_parameter_descriptors(orc::VideoSystem::NTSC, orc::SourceType::Composite);

        const auto* output_format = find_parameter(descriptors, "output_format");
        ASSERT_NE(output_format, nullptr);

        const auto& allowed = output_format->constraints.allowed_strings;
        EXPECT_FALSE(allowed.empty());
        EXPECT_FALSE(has_string(allowed, "rgb"));
        EXPECT_FALSE(has_string(allowed, "yuv"));
        EXPECT_FALSE(has_string(allowed, "y4m"));

#ifdef HAVE_FFMPEG
        EXPECT_TRUE(has_string(allowed, "mp4-h264"));
#else
        ASSERT_EQ(allowed.size(), 1U);
        EXPECT_EQ(allowed.front(), "mp4-h264");
#endif
    }

    TEST(FFmpegVideoSinkStageTest, decoderOptions_includeNtscPaths_forCompositeAndYc)
    {
        orc::FFmpegVideoSinkStage stage;

        for (const auto source_type : {orc::SourceType::Composite, orc::SourceType::YC}) {
            auto descriptors = stage.get_parameter_descriptors(orc::VideoSystem::NTSC, source_type);
            const auto* decoder_type = find_parameter(descriptors, "decoder_type");

            ASSERT_NE(decoder_type, nullptr);
            const auto& allowed = decoder_type->constraints.allowed_strings;

            ASSERT_TRUE(decoder_type->constraints.default_value.has_value());
            ASSERT_TRUE(std::holds_alternative<std::string>(*decoder_type->constraints.default_value));
            EXPECT_EQ(std::get<std::string>(*decoder_type->constraints.default_value), "ntsc2d");

            EXPECT_FALSE(has_string(allowed, "auto"));
            EXPECT_TRUE(has_string(allowed, "mono"));
            EXPECT_TRUE(has_string(allowed, "ntsc1d"));
            EXPECT_TRUE(has_string(allowed, "ntsc2d"));
            EXPECT_TRUE(has_string(allowed, "ntsc3d"));
            EXPECT_TRUE(has_string(allowed, "ntsc3dnoadapt"));
            EXPECT_FALSE(has_string(allowed, "pal2d"));
            EXPECT_FALSE(has_string(allowed, "transform2d"));
            EXPECT_FALSE(has_string(allowed, "transform3d"));

            if (source_type == orc::SourceType::YC) {
                EXPECT_NE(decoder_type->description.find("YC sources"), std::string::npos);
            } else {
                EXPECT_EQ(decoder_type->description.find("YC sources"), std::string::npos);
            }
        }
    }

    TEST(FFmpegVideoSinkStageTest, decoderOptions_includePalPaths_forCompositeAndYc)
    {
        orc::FFmpegVideoSinkStage stage;

        for (const auto source_type : {orc::SourceType::Composite, orc::SourceType::YC}) {
            auto descriptors = stage.get_parameter_descriptors(orc::VideoSystem::PAL, source_type);
            const auto* decoder_type = find_parameter(descriptors, "decoder_type");

            ASSERT_NE(decoder_type, nullptr);
            const auto& allowed = decoder_type->constraints.allowed_strings;

            ASSERT_TRUE(decoder_type->constraints.default_value.has_value());
            ASSERT_TRUE(std::holds_alternative<std::string>(*decoder_type->constraints.default_value));
            EXPECT_EQ(std::get<std::string>(*decoder_type->constraints.default_value), "pal2d");

            EXPECT_FALSE(has_string(allowed, "auto"));
            EXPECT_TRUE(has_string(allowed, "mono"));
            EXPECT_TRUE(has_string(allowed, "pal2d"));
            EXPECT_TRUE(has_string(allowed, "transform2d"));
            EXPECT_TRUE(has_string(allowed, "transform3d"));
            EXPECT_FALSE(has_string(allowed, "ntsc1d"));
            EXPECT_FALSE(has_string(allowed, "ntsc2d"));
            EXPECT_FALSE(has_string(allowed, "ntsc3d"));
            EXPECT_FALSE(has_string(allowed, "ntsc3dnoadapt"));

            if (source_type == orc::SourceType::YC) {
                EXPECT_NE(decoder_type->description.find("YC sources"), std::string::npos);
            } else {
                EXPECT_EQ(decoder_type->description.find("YC sources"), std::string::npos);
            }
        }
    }

    TEST(FFmpegVideoSinkStageTest, decoderOptions_includePalPaths_forPalM_forCompositeAndYc)
    {
        orc::FFmpegVideoSinkStage stage;

        for (const auto source_type : {orc::SourceType::Composite, orc::SourceType::YC}) {
            auto descriptors = stage.get_parameter_descriptors(orc::VideoSystem::PAL_M, source_type);
            const auto* decoder_type = find_parameter(descriptors, "decoder_type");

            ASSERT_NE(decoder_type, nullptr);
            const auto& allowed = decoder_type->constraints.allowed_strings;

            ASSERT_TRUE(decoder_type->constraints.default_value.has_value());
            ASSERT_TRUE(std::holds_alternative<std::string>(*decoder_type->constraints.default_value));
            EXPECT_EQ(std::get<std::string>(*decoder_type->constraints.default_value), "pal2d");

            EXPECT_FALSE(has_string(allowed, "auto"));
            EXPECT_TRUE(has_string(allowed, "mono"));
            EXPECT_TRUE(has_string(allowed, "pal2d"));
            EXPECT_TRUE(has_string(allowed, "transform2d"));
            EXPECT_TRUE(has_string(allowed, "transform3d"));
            EXPECT_FALSE(has_string(allowed, "ntsc1d"));
            EXPECT_FALSE(has_string(allowed, "ntsc2d"));
            EXPECT_FALSE(has_string(allowed, "ntsc3d"));
            EXPECT_FALSE(has_string(allowed, "ntsc3dnoadapt"));

            if (source_type == orc::SourceType::YC) {
                EXPECT_NE(decoder_type->description.find("YC sources"), std::string::npos);
            } else {
                EXPECT_EQ(decoder_type->description.find("YC sources"), std::string::npos);
            }
        }
    }

    TEST(FFmpegVideoSinkStageTest, setParameters_rejectsRawOutputFormats)
    {
        orc::FFmpegVideoSinkStage stage;

        const bool ok = stage.set_parameters({{"output_format", std::string("rgb")}});

        EXPECT_FALSE(ok);
    }

    TEST(FFmpegVideoSinkStageTest, setParameters_acceptsOrRejectsMp4DependingOnFfmpegBuild)
    {
        orc::FFmpegVideoSinkStage stage;

        const bool ok = stage.set_parameters({{"output_format", std::string("mp4-h264")}});

#ifdef HAVE_FFMPEG
        EXPECT_TRUE(ok);
#else
        EXPECT_FALSE(ok);
#endif
    }

    TEST(FFmpegVideoSinkStageTest, getParameters_keepsFfmpegSpecificControls)
    {
        orc::FFmpegVideoSinkStage stage;
        const auto params = stage.get_parameters();

        EXPECT_TRUE(params.find("encoder_preset") != params.end());
        EXPECT_TRUE(params.find("encoder_crf") != params.end());
        EXPECT_TRUE(params.find("encoder_bitrate") != params.end());
        EXPECT_TRUE(params.find("embed_audio") != params.end());
        EXPECT_TRUE(params.find("embed_closed_captions") != params.end());
    }
}
