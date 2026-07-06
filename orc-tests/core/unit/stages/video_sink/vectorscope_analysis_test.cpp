/*
 * File:        vectorscope_analysis_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for vectorscope extraction from ComponentFrame
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/core/analysis/vectorscope/vectorscope_analysis.h"

#include <gtest/gtest.h>
#include <orc/stage/orc_preview_carriers.h>

#include "../../../../orc/plugins/stages/sinks/common/decoders/componentframe.h"
#include "../../../../orc/plugins/stages/sinks/common/decoders/vectorscope_extract.h"

namespace orc_unit_test {
TEST(VectorscopeAnalysisTest,
     ExtractFromComponentFrame_UsesActivePictureWindow) {
  orc::SourceParameters source_parameters;
  source_parameters.system = orc::VideoSystem::NTSC;
  source_parameters.frame_width_nominal = 8;
  source_parameters.active_video_start = 2;
  source_parameters.active_video_end = 6;
  source_parameters.first_active_frame_line = 1;
  source_parameters.last_active_frame_line = 6;
  source_parameters.active_area_cropping_applied = false;  // absolute indexing
  // Set CVBS levels so normalization works correctly.  Active-area test values
  // (y*100+x ≈ 102-505) stay well below the clamp threshold; the outside-area
  // sentinel (10000) saturates to ±32767 after division by level_range (600).
  source_parameters.blanking_level = 200;
  source_parameters.white_level = 800;

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
        // Sentinel value that saturates to ±32767 after normalisation
        u_line[x] = 10000.0;
        v_line[x] = -10000.0;
      }
    }
  }

  const auto data = orc::extract_vectorscope_from_component_frame(
      frame, source_parameters, 42, 1);

  EXPECT_EQ(data.field_number, 42u);
  EXPECT_EQ(data.width, 4u);
  EXPECT_EQ(data.height, 5u);
  ASSERT_EQ(data.samples.size(), 20u);

  bool saw_first_field = false;
  bool saw_second_field = false;

  for (const auto& sample : data.samples) {
    // Active-area values (≤505) normalised by level_range (600) stay below
    // the ±32767 saturation ceiling that outside-area sentinels reach.
    EXPECT_LT(std::abs(sample.u), 32767.0);
    EXPECT_LT(std::abs(sample.v), 32767.0);

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

TEST(VectorscopeAnalysisTest,
     ExtractFromComponentFrame_UsesRelativeIndexing_WhenCroppingApplied) {
  // When active_area_cropping_applied=true the comb decoder writes U/V at
  // 0-based offsets: row 0 = first_active_frame_line, col 0 =
  // active_video_start. extract_vectorscope_from_component_frame must read the
  // same 0-based range so that the vectorscope samples match the decoded
  // chroma, not the empty frame margin.
  orc::SourceParameters sp;
  sp.system = orc::VideoSystem::NTSC;
  sp.frame_width_nominal = 10;
  sp.active_video_start = 3;
  sp.active_video_end = 7;  // active width = 4
  sp.first_active_frame_line = 2;
  sp.last_active_frame_line = 5;           // active height = 3
  sp.active_area_cropping_applied = true;  // relative (0-based) indexing
  sp.blanking_level = 200;
  sp.white_level = 800;  // level_range = 600

  ComponentFrame frame;
  frame.init(sp, false);

  // Write sentinel (saturating) values everywhere in the frame.
  for (int32_t y = 0; y < frame.getHeight(); ++y) {
    double* u = frame.u(y);
    double* v = frame.v(y);
    for (int32_t x = 0; x < frame.getWidth(); ++x) {
      u[x] = 10000.0;
      v[x] = -10000.0;
    }
  }

  // Write known values at the RELATIVE active area (rows 0..2, cols 0..3),
  // matching what the comb decoder does with active_area_cropping_applied=true.
  const int32_t active_w = sp.active_video_end - sp.active_video_start;  // 4
  const int32_t active_h =
      sp.last_active_frame_line - sp.first_active_frame_line;  // 3
  for (int32_t ry = 0; ry < active_h; ++ry) {
    double* u = frame.u(ry);
    double* v = frame.v(ry);
    for (int32_t rx = 0; rx < active_w; ++rx) {
      // Active values (ry*100+rx ≤ 203) stay below the saturation ceiling.
      u[rx] = static_cast<double>((ry * 100) + rx);
      v[rx] = -static_cast<double>((ry * 100) + rx);
    }
  }

  const auto data =
      orc::extract_vectorscope_from_component_frame(frame, sp, 7, 1);

  EXPECT_EQ(data.field_number, 7u);
  EXPECT_EQ(data.width, static_cast<uint32_t>(active_w));
  EXPECT_EQ(data.height, static_cast<uint32_t>(active_h));
  ASSERT_EQ(data.samples.size(), static_cast<size_t>(active_w * active_h));

  // All samples must come from the relative active area (not the sentinels).
  // Active values (≤203 / 600) stay well below ±32767.
  for (const auto& sample : data.samples) {
    EXPECT_LT(std::abs(sample.u), 32767.0)
        << "sentinel value found — reader used wrong (absolute) coordinates";
    EXPECT_LT(std::abs(sample.v), 32767.0)
        << "sentinel value found — reader used wrong (absolute) coordinates";
  }
}

TEST(VectorscopeAnalysisTest, ExtractFromColourFrameCarrier_CanUseFullFrame) {
  orc::ColourFrameCarrier carrier;
  carrier.system = orc::VideoSystem::NTSC;
  carrier.width = 6;
  carrier.height = 4;
  carrier.active_x_start = 1;
  carrier.active_x_end = 5;
  carrier.active_y_start = 1;
  carrier.active_y_end = 3;
  // uv_range = 900: active-area values (y*100+x ≤ 204) stay below the clamp
  // threshold; the sentinel (10000) saturates to ±32767.
  carrier.cvbs_blanking = 100.0;
  carrier.cvbs_white = 1000.0;

  const size_t sample_count =
      static_cast<size_t>(carrier.width) * static_cast<size_t>(carrier.height);
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
        // Sentinel value that saturates to ±32767 after normalisation
        carrier.u_plane[index] = 10000.0;
        carrier.v_plane[index] = -10000.0;
      }
    }
  }

  const auto active_data =
      orc::VectorscopeAnalysisTool::extractFromColourFrameCarrier(carrier, 12,
                                                                  1, true);
  const auto full_data =
      orc::VectorscopeAnalysisTool::extractFromColourFrameCarrier(carrier, 12,
                                                                  1, false);

  EXPECT_EQ(active_data.field_number, 12u);
  EXPECT_EQ(active_data.width, 4u);
  EXPECT_EQ(active_data.height, 2u);
  ASSERT_EQ(active_data.samples.size(), 8u);

  EXPECT_EQ(full_data.field_number, 12u);
  EXPECT_EQ(full_data.width, 6u);
  EXPECT_EQ(full_data.height, 4u);
  ASSERT_EQ(full_data.samples.size(), 24u);

  // Active-area values (≤204) normalised by uv_range (900) stay below the
  // ±32767 saturation ceiling that outside-area sentinels reach.
  for (const auto& sample : active_data.samples) {
    EXPECT_LT(std::abs(sample.u), 32767.0);
    EXPECT_LT(std::abs(sample.v), 32767.0);
  }

  bool saw_full_frame_only_sample = false;
  for (const auto& sample : full_data.samples) {
    // Outside-area sentinels (10000 / uv_range 900 > 1) saturate to ±32767.
    if (std::abs(sample.u) == 32767.0 && std::abs(sample.v) == 32767.0) {
      saw_full_frame_only_sample = true;
      break;
    }
  }

  EXPECT_TRUE(saw_full_frame_only_sample);
}
}  // namespace orc_unit_test