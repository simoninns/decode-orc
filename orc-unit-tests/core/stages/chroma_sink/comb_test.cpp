/*
 * File:        comb_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for Comb decoder core paths
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <cmath>

#include "../../../../orc/core/stages/chroma_sink/decoders/comb.h"

namespace orc_unit_test
{
    namespace
    {
        orc::SourceParameters make_ntsc_video_params()
        {
            orc::SourceParameters p;
            p.system = orc::VideoSystem::NTSC;
            p.field_width = 32;
            p.field_height = 4;
            p.active_video_start = 16;
            p.active_video_end = 24;
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
            field.field_phase_id = is_first_field ? 1 : 2;

            constexpr int width = 32;
            constexpr int height = 4;
            field.luma_data.reserve(width * height);
            field.chroma_data.reserve(width * height);

            for (int line = 0; line < height; ++line) {
                for (int x = 0; x < width; ++x) {
                    field.luma_data.push_back(static_cast<uint16_t>(luma_base + line * 4 + x));
                    field.chroma_data.push_back(static_cast<uint16_t>(chroma_base + ((x % 4) * 10)));
                }
            }

            return field;
        }

        SourceField make_composite_field(bool is_first_field, uint16_t base)
        {
            SourceField field;
            field.is_yc = false;
            field.is_first_field = is_first_field;
            field.field_phase_id = is_first_field ? 1 : 2;

            constexpr int width = 32;
            constexpr int height = 4;
            field.data.reserve(width * height);

            for (int line = 0; line < height; ++line) {
                for (int x = 0; x < width; ++x) {
                    field.data.push_back(static_cast<uint16_t>(base + line * 8 + x));
                }
            }

            return field;
        }
    }

    TEST(CombTest, configurationLookAround_dependsOnDimensions)
    {
        Comb::Configuration c2d;
        c2d.dimensions = 2;

        Comb::Configuration c3d;
        c3d.dimensions = 3;

        EXPECT_EQ(c2d.getLookBehind(), 0);
        EXPECT_EQ(c2d.getLookAhead(), 0);
        EXPECT_EQ(c3d.getLookBehind(), 1);
        EXPECT_EQ(c3d.getLookAhead(), 1);
    }

    TEST(CombTest, decodeFrames_ycPath_preservesLumaInActiveRegion)
    {
        const auto params = make_ntsc_video_params();

        Comb::Configuration config;
        config.dimensions = 2;
        config.phaseCompensation = false;

        Comb decoder;
        decoder.updateConfiguration(params, config);

        auto first_field = make_yc_field(true, 1000, 2000);
        auto second_field = make_yc_field(false, 3000, 4000);

        std::vector<SourceField> fields = {first_field, second_field};
        std::vector<ComponentFrame> output(1);

        decoder.decodeFrames(fields, 0, 2, output);

        EXPECT_EQ(output[0].getWidth(), 32);
        EXPECT_EQ(output[0].getHeight(), 7);

        const double* line0 = output[0].y(0);
        const double* line1 = output[0].y(1);

        EXPECT_DOUBLE_EQ(line0[16], static_cast<double>(first_field.luma_data[16]));
        EXPECT_DOUBLE_EQ(line0[20], static_cast<double>(first_field.luma_data[20]));
        EXPECT_DOUBLE_EQ(line1[16], static_cast<double>(second_field.luma_data[16]));
        EXPECT_DOUBLE_EQ(line1[20], static_cast<double>(second_field.luma_data[20]));
    }

    TEST(CombTest, decodeFrames_compositePath_matchesGoldenVector)
    {
        const auto params = make_ntsc_video_params();

        Comb::Configuration config;
        config.dimensions = 2;
        config.phaseCompensation = false;

        Comb decoder;
        decoder.updateConfiguration(params, config);

        auto first_field = make_composite_field(true, 2500);
        auto second_field = make_composite_field(false, 2600);

        std::vector<SourceField> fields = {first_field, second_field};
        std::vector<ComponentFrame> output(1);

        decoder.decodeFrames(fields, 0, 2, output);

        EXPECT_EQ(output[0].getWidth(), 32);
        EXPECT_EQ(output[0].getHeight(), 7);

        const double* y0 = output[0].y(0);
        const double* u0 = output[0].u(0);
        const double* v0 = output[0].v(0);

        EXPECT_DOUBLE_EQ(y0[16], 2516.0);
        EXPECT_DOUBLE_EQ(y0[20], 2520.0);
        EXPECT_DOUBLE_EQ(u0[16], 0.0);
        EXPECT_DOUBLE_EQ(u0[20], 0.0);
        EXPECT_DOUBLE_EQ(v0[16], 0.0);
        EXPECT_DOUBLE_EQ(v0[20], 0.0);
        EXPECT_TRUE(std::isfinite(y0[16]));
        EXPECT_TRUE(std::isfinite(u0[16]));
        EXPECT_TRUE(std::isfinite(v0[16]));
    }
}
