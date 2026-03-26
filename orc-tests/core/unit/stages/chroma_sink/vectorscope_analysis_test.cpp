/*
 * File:        vectorscope_analysis_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for vectorscope extraction from ComponentFrame
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include "../../../../orc/core/analysis/vectorscope/vectorscope_analysis.h"
#include "../../../../orc/core/stages/chroma_sink/decoders/componentframe.h"
#include "../../../../orc/view-types/orc_preview_carriers.h"

namespace orc_unit_test
{
TEST(VectorscopeAnalysisTest, extractFromComponentFrameUsesActivePictureWindow)
{
    orc::SourceParameters source_parameters;
    source_parameters.field_width = 8;
    source_parameters.field_height = 4;
    source_parameters.active_video_start = 2;
    source_parameters.active_video_end = 6;
    source_parameters.first_active_frame_line = 1;
    source_parameters.last_active_frame_line = 6;

    ComponentFrame frame;
    frame.init(source_parameters, false);

    for (int32_t y = 0; y < frame.getHeight(); ++y) {
        double* u_line = frame.u(y);
        double* v_line = frame.v(y);

        for (int32_t x = 0; x < frame.getWidth(); ++x) {
            const bool inside_active =
                y >= source_parameters.first_active_frame_line &&
                y < source_parameters.last_active_frame_line &&
                x >= source_parameters.active_video_start &&
                x < source_parameters.active_video_end;

            if (inside_active) {
                u_line[x] = static_cast<double>((y * 100) + x);
                v_line[x] = -static_cast<double>((y * 100) + x);
            } else {
                u_line[x] = 10000.0;
                v_line[x] = -10000.0;
            }
        }
    }

    const auto data = orc::VectorscopeAnalysisTool::extractFromComponentFrame(
        frame,
        source_parameters,
        42,
        1);

    EXPECT_EQ(data.field_number, 42u);
    EXPECT_EQ(data.width, 4u);
    EXPECT_EQ(data.height, 5u);
    ASSERT_EQ(data.samples.size(), 20u);

    bool saw_first_field = false;
    bool saw_second_field = false;

    for (const auto& sample : data.samples) {
        EXPECT_LT(std::abs(sample.u), 10000.0);
        EXPECT_LT(std::abs(sample.v), 10000.0);

        if (sample.field_id == 0) {
            saw_first_field = true;
        }
        if (sample.field_id == 1) {
            saw_second_field = true;
        }
    }

    EXPECT_TRUE(saw_first_field);
    EXPECT_TRUE(saw_second_field);
}

TEST(VectorscopeAnalysisTest, extractFromColourFrameCarrierCanUseFullFrame)
{
    orc::ColourFrameCarrier carrier;
    carrier.system = orc::VideoSystem::NTSC;
    carrier.width = 6;
    carrier.height = 4;
    carrier.active_x_start = 1;
    carrier.active_x_end = 5;
    carrier.active_y_start = 1;
    carrier.active_y_end = 3;
    carrier.black_16b_ire = 100.0;
    carrier.white_16b_ire = 200.0;

    const size_t sample_count = static_cast<size_t>(carrier.width) * static_cast<size_t>(carrier.height);
    carrier.y_plane.assign(sample_count, 0.0);
    carrier.u_plane.resize(sample_count);
    carrier.v_plane.resize(sample_count);

    for (uint32_t y = 0; y < carrier.height; ++y) {
        for (uint32_t x = 0; x < carrier.width; ++x) {
            const size_t index = static_cast<size_t>(y) * carrier.width + x;
            const bool inside_active =
                y >= carrier.active_y_start && y < carrier.active_y_end &&
                x >= carrier.active_x_start && x < carrier.active_x_end;

            if (inside_active) {
                carrier.u_plane[index] = static_cast<double>((y * 100) + x);
                carrier.v_plane[index] = -static_cast<double>((y * 100) + x);
            } else {
                carrier.u_plane[index] = 10000.0;
                carrier.v_plane[index] = -10000.0;
            }
        }
    }

    const auto active_data = orc::VectorscopeAnalysisTool::extractFromColourFrameCarrier(
        carrier,
        12,
        1,
        true);
    const auto full_data = orc::VectorscopeAnalysisTool::extractFromColourFrameCarrier(
        carrier,
        12,
        1,
        false);

    EXPECT_EQ(active_data.field_number, 12u);
    EXPECT_EQ(active_data.width, 4u);
    EXPECT_EQ(active_data.height, 2u);
    ASSERT_EQ(active_data.samples.size(), 8u);

    EXPECT_EQ(full_data.field_number, 12u);
    EXPECT_EQ(full_data.width, 6u);
    EXPECT_EQ(full_data.height, 4u);
    ASSERT_EQ(full_data.samples.size(), 24u);

    for (const auto& sample : active_data.samples) {
        EXPECT_LT(std::abs(sample.u), 10000.0);
        EXPECT_LT(std::abs(sample.v), 10000.0);
    }

    bool saw_full_frame_only_sample = false;
    for (const auto& sample : full_data.samples) {
        if (std::abs(sample.u) == 10000.0 && std::abs(sample.v) == 10000.0) {
            saw_full_frame_only_sample = true;
            break;
        }
    }

    EXPECT_TRUE(saw_full_frame_only_sample);
}
}