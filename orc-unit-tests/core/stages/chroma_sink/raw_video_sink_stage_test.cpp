/*
 * File:        raw_video_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for RawVideoSinkStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "../../../../orc/core/stages/chroma_sink/raw_video_sink_stage.h"

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

    TEST(RawVideoSinkStageTest, parameterDescriptors_includeOnlyRawOutputFormats)
    {
        orc::RawVideoSinkStage stage;
        auto descriptors = stage.get_parameter_descriptors(orc::VideoSystem::NTSC, orc::SourceType::Composite);

        const auto* output_format = find_parameter(descriptors, "output_format");
        ASSERT_NE(output_format, nullptr);

        const auto& allowed = output_format->constraints.allowed_strings;
        ASSERT_EQ(allowed.size(), 3U);
        EXPECT_TRUE(has_string(allowed, "rgb"));
        EXPECT_TRUE(has_string(allowed, "yuv"));
        EXPECT_TRUE(has_string(allowed, "y4m"));
        EXPECT_FALSE(has_string(allowed, "mp4-h264"));
        EXPECT_FALSE(has_string(allowed, "mkv-ffv1"));
    }

    TEST(RawVideoSinkStageTest, parameterDescriptors_hideFfmpegOnlyControls)
    {
        orc::RawVideoSinkStage stage;
        auto descriptors = stage.get_parameter_descriptors(orc::VideoSystem::NTSC, orc::SourceType::Composite);

        EXPECT_EQ(find_parameter(descriptors, "encoder_preset"), nullptr);
        EXPECT_EQ(find_parameter(descriptors, "encoder_crf"), nullptr);
        EXPECT_EQ(find_parameter(descriptors, "encoder_bitrate"), nullptr);
        EXPECT_EQ(find_parameter(descriptors, "embed_audio"), nullptr);
        EXPECT_EQ(find_parameter(descriptors, "embed_closed_captions"), nullptr);
        EXPECT_EQ(find_parameter(descriptors, "hardware_encoder"), nullptr);
        EXPECT_EQ(find_parameter(descriptors, "prores_profile"), nullptr);
        EXPECT_EQ(find_parameter(descriptors, "use_lossless_mode"), nullptr);
        EXPECT_EQ(find_parameter(descriptors, "apply_deinterlace"), nullptr);
    }

    TEST(RawVideoSinkStageTest, decoderOptions_includeNtscPaths_forCompositeAndYc)
    {
        orc::RawVideoSinkStage stage;

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

    TEST(RawVideoSinkStageTest, decoderOptions_includePalPaths_forCompositeAndYc)
    {
        orc::RawVideoSinkStage stage;

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

    TEST(RawVideoSinkStageTest, setParameters_rejectsEncodedFormats)
    {
        orc::RawVideoSinkStage stage;

        const bool ok = stage.set_parameters({{"output_format", std::string("mp4-h264")}});

        EXPECT_FALSE(ok);
    }

    TEST(RawVideoSinkStageTest, setParameters_acceptsRawFormatAndIgnoresFfmpegKeys)
    {
        orc::RawVideoSinkStage stage;

        const bool ok = stage.set_parameters({
            {"output_format", std::string("y4m")},
            {"encoder_preset", std::string("fast")},
            {"embed_audio", true},
            {"embed_closed_captions", true}
        });

        EXPECT_TRUE(ok);

        const auto params = stage.get_parameters();
        auto format_it = params.find("output_format");
        ASSERT_NE(format_it, params.end());
        ASSERT_TRUE(std::holds_alternative<std::string>(format_it->second));
        EXPECT_EQ(std::get<std::string>(format_it->second), "y4m");

        EXPECT_TRUE(params.find("encoder_preset") == params.end());
        EXPECT_TRUE(params.find("encoder_crf") == params.end());
        EXPECT_TRUE(params.find("encoder_bitrate") == params.end());
        EXPECT_TRUE(params.find("embed_audio") == params.end());
        EXPECT_TRUE(params.find("embed_closed_captions") == params.end());
    }
}
