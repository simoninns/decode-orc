/*
 * File:        palcolour_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for PalColour decoder core paths
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include "../../../../orc/core/stages/chroma_sink/decoders/palcolour.h"

namespace orc_unit_test
{
    namespace
    {
        orc::SourceParameters make_pal_video_params()
        {
            orc::SourceParameters p;
            p.system = orc::VideoSystem::PAL;
            p.field_width = 64;
            p.field_height = 4;
            p.active_video_start = 16;
            p.active_video_end = 32;
            p.first_active_frame_line = 0;
            p.last_active_frame_line = 6;
            p.colour_burst_start = 16;
            p.colour_burst_end = 20;
            p.black_16b_ire = 0;
            p.white_16b_ire = 100;
            p.sample_rate = 4.0;
            p.fsc = 1.0;
            return p;
        }

        SourceField make_yc_field(bool is_first_field, uint16_t luma_base, uint16_t chroma_base)
        {
            SourceField field;
            field.is_yc = true;
            field.is_first_field = is_first_field;

            constexpr int width = 64;
            constexpr int height = 4;
            field.luma_data.reserve(width * height);
            field.chroma_data.reserve(width * height);

            for (int line = 0; line < height; ++line) {
                for (int x = 0; x < width; ++x) {
                    field.luma_data.push_back(static_cast<uint16_t>(luma_base + line * 8 + x));
                    field.chroma_data.push_back(static_cast<uint16_t>(chroma_base + ((x % 4) * 12)));
                }
            }

            return field;
        }

        SourceField make_composite_field(bool is_first_field, uint16_t base)
        {
            SourceField field;
            field.is_yc = false;
            field.is_first_field = is_first_field;

            constexpr int width = 64;
            constexpr int height = 4;
            field.data.reserve(width * height);

            for (int line = 0; line < height; ++line) {
                for (int x = 0; x < width; ++x) {
                    field.data.push_back(static_cast<uint16_t>(base + line * 5 + x));
                }
            }

            return field;
        }
    }

    TEST(PalColourTest, configurationLookAround_matchesFilterMode)
    {
        PalColour::Configuration pal2d;
        pal2d.chromaFilter = PalColour::palColourFilter;

        PalColour::Configuration transform3d;
        transform3d.chromaFilter = PalColour::transform3DFilter;

        EXPECT_EQ(pal2d.getLookBehind(), 0);
        EXPECT_EQ(pal2d.getLookAhead(), 0);
        EXPECT_GT(transform3d.getLookBehind(), 0);
        EXPECT_GT(transform3d.getLookAhead(), 0);
    }

    TEST(PalColourTest, decodeFrames_ycPath_preservesLumaInActiveRegion)
    {
        const auto params = make_pal_video_params();

        PalColour::Configuration config;
        config.chromaFilter = PalColour::palColourFilter;
        config.yNRLevel = 0.0;

        PalColour decoder;
        decoder.updateConfiguration(params, config);

        auto first_field = make_yc_field(true, 1200, 2200);
        auto second_field = make_yc_field(false, 3200, 4200);

        std::vector<SourceField> fields = {first_field, second_field};
        std::vector<ComponentFrame> output(1);

        decoder.decodeFrames(fields, 0, 2, output);

        EXPECT_EQ(output[0].getWidth(), 64);
        EXPECT_EQ(output[0].getHeight(), 7);

        const double* line0 = output[0].y(0);
        const double* line1 = output[0].y(1);

        EXPECT_DOUBLE_EQ(line0[16], static_cast<double>(first_field.luma_data[16]));
        EXPECT_DOUBLE_EQ(line0[24], static_cast<double>(first_field.luma_data[24]));
        EXPECT_DOUBLE_EQ(line1[16], static_cast<double>(second_field.luma_data[16]));
        EXPECT_DOUBLE_EQ(line1[24], static_cast<double>(second_field.luma_data[24]));
    }

    TEST(PalColourTest, decodeFrames_compositePath_matchesGoldenVector)
    {
        const auto params = make_pal_video_params();

        PalColour::Configuration config;
        config.chromaFilter = PalColour::palColourFilter;
        config.yNRLevel = 0.0;

        PalColour decoder;
        decoder.updateConfiguration(params, config);

        auto first_field = make_composite_field(true, 3000);
        auto second_field = make_composite_field(false, 3100);

        std::vector<SourceField> fields = {first_field, second_field};
        std::vector<ComponentFrame> output(1);

        decoder.decodeFrames(fields, 0, 2, output);

        EXPECT_EQ(output[0].getWidth(), 64);
        EXPECT_EQ(output[0].getHeight(), 7);

        const double* y0 = output[0].y(0);
        const double* u0 = output[0].u(0);
        const double* v0 = output[0].v(0);

        EXPECT_DOUBLE_EQ(y0[16], -3016.0);
        EXPECT_DOUBLE_EQ(y0[24], -3024.0);
        EXPECT_NEAR(u0[16], 1.1136, 1e-5);
        EXPECT_NEAR(u0[24], 1.11655, 1e-5);
        EXPECT_NEAR(v0[16], -0.3712, 1e-5);
        EXPECT_NEAR(v0[24], -0.372185, 1e-5);
    }
}
