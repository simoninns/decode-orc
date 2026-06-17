/*
 * File:        vectorscope_analysis.cpp
 * Module:      orc-core
 * Purpose:     Vectorscope analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "vectorscope_analysis.h"

#include <cvbs_signal_constants.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "../../../plugins/stages/sinks/common/decoders/componentframe.h"
#include "../../include/video_frame_representation.h"
#include "../analysis_registry.h"
#include "logging.h"

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

double clamp_normalized(double value) {
  if (value > 1.0) {
    return 1.0;
  }
  if (value < -1.0) {
    return -1.0;
  }
  return value;
}

}  // namespace

namespace orc {

std::string VectorscopeAnalysisTool::id() const { return "vectorscope"; }

std::string VectorscopeAnalysisTool::name() const { return "Vectorscope"; }

std::string VectorscopeAnalysisTool::description() const {
  return "Display U/V color components on a vectorscope for decoded chroma "
         "output";
}

std::string VectorscopeAnalysisTool::category() const {
  return "Visualization";
}

std::vector<ParameterDescriptor> VectorscopeAnalysisTool::parameters() const {
  // No batch parameters - this is a live visualization tool
  return {};
}

bool VectorscopeAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
  // Works with any source that has been chroma decoded
  (void)source_type;
  return true;
}

bool VectorscopeAnalysisTool::isApplicableToStage(
    const std::string& stage_name) const {
  // Vectorscope is exposed via preview views, not Stage Tools.
  (void)stage_name;
  return false;
}

AnalysisResult VectorscopeAnalysisTool::analyze(const AnalysisContext& ctx,
                                                AnalysisProgress* progress) {
  (void)ctx;  // Currently unused
  AnalysisResult result;

  // This is a live visualization tool, not a batch analysis
  // The GUI will call extractFromRGB() directly for each field
  // This method exists to satisfy the AnalysisTool interface

  if (progress) {
    progress->setStatus("Vectorscope is a live visualization tool");
    progress->setProgress(100);
  }

  result.status = AnalysisResult::Success;
  result.summary = "Vectorscope visualization active";

  ORC_LOG_DEBUG("Vectorscope analysis called (live tool, no batch processing)");

  return result;
}

bool VectorscopeAnalysisTool::canApplyToGraph() const {
  // Live visualization, nothing to apply
  return false;
}

bool VectorscopeAnalysisTool::applyToGraph(AnalysisResult& result,
                                           const Project& project,
                                           NodeID node_id) {
  (void)result;
  (void)project;
  (void)node_id;

  // Live visualization, nothing to apply
  return false;
}

int VectorscopeAnalysisTool::estimateDurationSeconds(
    const AnalysisContext& ctx) const {
  (void)ctx;

  // Live tool, instantaneous
  return 0;
}

VectorscopeData VectorscopeAnalysisTool::extractFromRGB(
    const uint16_t* rgb_data, uint32_t width, uint32_t height,
    uint64_t field_number, uint32_t subsample, uint8_t field_id) {
  VectorscopeData data;
  data.width = width;
  data.height = height;
  data.field_number = field_number;

  if (!rgb_data || width == 0 || height == 0 || subsample == 0) {
    return data;
  }

  // Reserve space for samples (with subsampling)
  size_t estimated_samples =
      static_cast<size_t>(width / subsample) * (height / subsample);
  data.samples.reserve(estimated_samples);

  // Extract U/V from RGB
  for (uint32_t y = 0; y < height; y += subsample) {
    for (uint32_t x = 0; x < width; x += subsample) {
      size_t pixel_index = (static_cast<size_t>(y) * width + x) * 3;

      uint16_t r = rgb_data[pixel_index + 0];
      uint16_t g = rgb_data[pixel_index + 1];
      uint16_t b = rgb_data[pixel_index + 2];

      UVSample uv = rgb_to_uv(r, g, b);
      uv.field_id = field_id;  // Track which field this sample came from
      data.samples.push_back(uv);
    }
  }

  ORC_LOG_DEBUG(
      "Extracted {} U/V samples from field {} ({}x{}, subsample={}, "
      "field_id={})",
      data.samples.size(), field_number, width, height, subsample, field_id);

  return data;
}

VectorscopeData VectorscopeAnalysisTool::extractFromInterlacedRGB(
    const uint16_t* rgb_data, uint32_t width, uint32_t height,
    uint64_t field_number, uint32_t subsample) {
  VectorscopeData data;
  data.width = width;
  data.height = height;
  data.field_number = field_number;

  if (!rgb_data || width == 0 || height == 0 || subsample == 0) {
    return data;
  }

  // Reserve space for samples from both fields (with subsampling)
  size_t estimated_samples =
      static_cast<size_t>(width / subsample) * (height / subsample);
  data.samples.reserve(estimated_samples);

  // Process both fields separately
  // Field 0 (first/odd field): even lines (0, 2, 4, ...)
  // Field 1 (second/even field): odd lines (1, 3, 5, ...)
  for (uint8_t field_id = 0; field_id < 2; field_id++) {
    // Process every (2 * subsample)th line starting from field_id
    for (uint32_t y = field_id; y < height; y += (2 * subsample)) {
      for (uint32_t x = 0; x < width; x += subsample) {
        size_t pixel_index = (static_cast<size_t>(y) * width + x) * 3;

        uint16_t r = rgb_data[pixel_index + 0];
        uint16_t g = rgb_data[pixel_index + 1];
        uint16_t b = rgb_data[pixel_index + 2];

        UVSample uv = rgb_to_uv(r, g, b);
        uv.field_id = field_id;  // Tag which field this sample came from
        data.samples.push_back(uv);
      }
    }
  }

  ORC_LOG_DEBUG(
      "Extracted {} U/V samples from interlaced frame {} ({}x{}, subsample={}, "
      "both fields)",
      data.samples.size(), field_number, width, height, subsample);

  return data;
}

VectorscopeData VectorscopeAnalysisTool::extractFromComponentFrame(
    const ::ComponentFrame& frame,
    const ::orc::SourceParameters& video_parameters, uint64_t field_number,
    uint32_t subsample) {
  VectorscopeData data;
  const int32_t width = frame.getWidth();
  const int32_t height = frame.getHeight();
  data.field_number = field_number;

  if (width == 0 || height == 0 || subsample == 0) {
    return data;
  }

  int32_t x_start = 0;
  int32_t x_end = width;
  int32_t y_start = 0;
  int32_t y_end = height;

  if (video_parameters.active_video_start >= 0 &&
      video_parameters.active_video_end > video_parameters.active_video_start &&
      video_parameters.active_video_end <= width) {
    x_start = video_parameters.active_video_start;
    x_end = video_parameters.active_video_end;
  }

  if (video_parameters.first_active_frame_line >= 0 &&
      video_parameters.last_active_frame_line >
          video_parameters.first_active_frame_line &&
      video_parameters.last_active_frame_line <= height) {
    y_start = video_parameters.first_active_frame_line;
    y_end = video_parameters.last_active_frame_line;
  }

  data.width = static_cast<uint32_t>(x_end - x_start);
  data.height = static_cast<uint32_t>(y_end - y_start);

  // Reserve space for samples from the active picture area only.
  const size_t active_width = static_cast<size_t>(x_end - x_start);
  const size_t active_height = static_cast<size_t>(y_end - y_start);
  size_t estimated_samples =
      (active_width / subsample) * (active_height / subsample);
  data.samples.reserve(estimated_samples);

  // Normalise CVBS-domain U/V to the ±32767 scale expected by UVSample.
  // ComponentFrame U/V planes carry values in the CVBS decoder domain;
  // dividing by the active-video voltage range (blanking→white) maps them
  // to approximately ±1, consistent with render_preview_from_colour_carrier.
  const double level_range =
      std::max(1.0, static_cast<double>(video_parameters.white_level -
                                        video_parameters.blanking_level));

  // Process both fields separately
  // Field 0 (first/odd field): even lines (0, 2, 4, ...)
  // Field 1 (second/even field): odd lines (1, 3, 5, ...)
  for (uint8_t field_id = 0; field_id < 2; field_id++) {
    int32_t first_y = y_start;
    if ((first_y & 1) != field_id) {
      ++first_y;
    }

    // Process every (2 * subsample)th line starting from field_id
    for (int32_t y = first_y; y < y_end;
         y += (2 * static_cast<int32_t>(subsample))) {
      const double* uLine = frame.u(y);
      const double* vLine = frame.v(y);

      for (int32_t x = x_start; x < x_end;
           x += static_cast<int32_t>(subsample)) {
        UVSample uv;
        uv.u = clamp_normalized(uLine[x] / level_range) * 32767.0;
        uv.v = clamp_normalized(vLine[x] / level_range) * 32767.0;
        uv.field_id = field_id;  // Tag which field this sample came from
        data.samples.push_back(uv);
      }
    }
  }

  ORC_LOG_DEBUG(
      "Extracted {} native U/V samples from ComponentFrame field {} (active "
      "{}x{} within {}x{}, subsample={}, both fields)",
      data.samples.size(), field_number, data.width, data.height, width, height,
      subsample);

  return data;
}

VectorscopeData VectorscopeAnalysisTool::extractFromColourFrameCarrier(
    const ColourFrameCarrier& carrier, uint64_t field_number,
    uint32_t subsample, bool active_area_only) {
  VectorscopeData data;
  data.field_number = field_number;
  data.system = carrier.system;
  // CVBS_U10_4FSC normative levels from carrier (set by chroma_sink).
  data.cvbs_white = static_cast<int32_t>(carrier.cvbs_white);
  data.cvbs_blanking = static_cast<int32_t>(carrier.cvbs_blanking);

  if (!carrier.is_valid() || subsample == 0) {
    return data;
  }

  uint32_t x_start = 0;
  uint32_t x_end = carrier.width;
  uint32_t y_start = 0;
  uint32_t y_end = carrier.height;

  if (active_area_only) {
    if (carrier.active_x_end > carrier.active_x_start &&
        carrier.active_x_end <= carrier.width) {
      x_start = carrier.active_x_start;
      x_end = carrier.active_x_end;
    }

    if (carrier.active_y_end > carrier.active_y_start &&
        carrier.active_y_end <= carrier.height) {
      y_start = carrier.active_y_start;
      y_end = carrier.active_y_end;
    }
  }

  data.width = x_end - x_start;
  data.height = y_end - y_start;

  const size_t sample_width = static_cast<size_t>(x_end - x_start);
  const size_t sample_height = static_cast<size_t>(y_end - y_start);
  const size_t estimated_samples =
      (sample_width / subsample) * (sample_height / subsample);
  data.samples.reserve(estimated_samples);

  // Normalise CVBS-domain U/V to the ±32767 scale expected by UVSample.
  const double uv_range =
      std::max(1.0, carrier.cvbs_white - carrier.cvbs_blanking);

  for (uint8_t field_id = 0; field_id < 2; ++field_id) {
    uint32_t first_y = y_start;
    if ((first_y & 1U) != field_id) {
      ++first_y;
    }

    for (uint32_t y = first_y; y < y_end; y += (2 * subsample)) {
      const size_t line_offset =
          static_cast<size_t>(y) * static_cast<size_t>(carrier.width);

      for (uint32_t x = x_start; x < x_end; x += subsample) {
        const size_t sample_index = line_offset + static_cast<size_t>(x);
        UVSample uv;
        uv.u = clamp_normalized(carrier.u_plane[sample_index] / uv_range) *
               32767.0;
        uv.v = clamp_normalized(carrier.v_plane[sample_index] / uv_range) *
               32767.0;
        uv.field_id = field_id;
        data.samples.push_back(uv);
      }
    }
  }

  ORC_LOG_DEBUG(
      "Extracted {} U/V samples from colour preview carrier field {} ({} area "
      "{}x{} within {}x{}, subsample={}, both fields)",
      data.samples.size(), field_number, active_area_only ? "active" : "full",
      data.width, data.height, carrier.width, carrier.height, subsample);

  return data;
}

VectorscopeData VectorscopeAnalysisTool::extractFromCompositeRepresentation(
    const VideoFrameRepresentation& representation, FrameID frame_id,
    uint32_t subsample, bool active_area_only) {
  VectorscopeData data;
  data.field_number = frame_id;

  const auto params_opt = representation.get_video_parameters();
  if (!params_opt.has_value() || subsample == 0) {
    return data;
  }
  const SourceParameters& vp = *params_opt;

  data.system = vp.system;
  // CVBS_U10_4FSC normative levels from source parameters.
  data.cvbs_white = vp.white_level;
  data.cvbs_blanking = vp.blanking_level;

  if (vp.frame_width_nominal <= 0 || vp.frame_height <= 0 ||
      vp.blanking_level < 0 || vp.white_level <= vp.blanking_level) {
    return data;
  }

  const size_t frame_width = static_cast<size_t>(vp.frame_width_nominal);
  const size_t frame_height = static_cast<size_t>(vp.frame_height);

  // Active area bounds
  size_t x_start = 0;
  size_t x_end = frame_width;
  size_t y_start = 0;
  size_t y_end = frame_height;

  if (active_area_only) {
    if (vp.active_video_start >= 0 &&
        vp.active_video_end > vp.active_video_start) {
      x_start = static_cast<size_t>(std::min<int32_t>(
          vp.active_video_start, static_cast<int32_t>(frame_width)));
      x_end = static_cast<size_t>(std::min<int32_t>(
          vp.active_video_end, static_cast<int32_t>(frame_width)));
    }
    if (vp.first_active_frame_line >= 0 &&
        vp.last_active_frame_line > vp.first_active_frame_line) {
      y_start = static_cast<size_t>(std::min<int32_t>(
          vp.first_active_frame_line, static_cast<int32_t>(frame_height)));
      y_end = static_cast<size_t>(std::min<int32_t>(
          vp.last_active_frame_line, static_cast<int32_t>(frame_height)));
    }
  }

  if (x_start >= x_end || y_start >= y_end) {
    return data;
  }

  data.width = static_cast<uint32_t>(x_end - x_start);
  data.height = static_cast<uint32_t>(y_end - y_start);

  const bool has_chroma = representation.has_separate_channels();

  // 10-bit domain normalization: blanking_level → 0.0, white_level → 1.0
  const double blank = static_cast<double>(vp.blanking_level);
  const double level_range =
      std::max(1.0, static_cast<double>(vp.white_level - vp.blanking_level));

  // Phase step: CVBS_U10_4FSC is nominally 4×Fsc — the 4-point table
  // {1,0,-1,0}/{0,-1,0,1} is valid (subcarrier phase π/2 at sample x=0).
  // The continuous fallback uses the same π/2 starting phase for consistency.
  // EBU Tech. 3280-E §1.1 (PAL) / SMPTE 244M-2003 §4.1 (NTSC) /
  // ITU-R BT.1700-1 Annex 1 Part B (PAL_M).
  const double sys_fsc = fsc_from_system(vp.system);
  const double sys_sample_rate = sample_rate_from_system(vp.system);
  const bool approx_4fsc = std::abs((sys_sample_rate / sys_fsc) - 4.0) < 1.0e-3;
  const double phase_step = 2.0 * kPi * sys_fsc / sys_sample_rate;

  // Determine field1 boundary for PAL V-alternation in frame-flat context.
  // PAL: field1 = lines [0, 312], field2 = lines [313, 624].
  // NTSC/PAL_M: field1 = lines [0, 261], field2 = lines [262, 524].
  // ITU-R BT.470-6 §1.1 (PAL) / SMPTE 170M-2004 §11.3 (NTSC).
  size_t field1_line_count = 0;
  if (vp.system == VideoSystem::PAL) {
    field1_line_count = 313;
  } else if (vp.system == VideoSystem::NTSC ||
             vp.system == VideoSystem::PAL_M) {
    field1_line_count = 262;
  }

  data.samples.reserve(((x_end - x_start) / subsample) *
                       ((y_end - y_start) / subsample));

  for (size_t y = y_start; y < y_end; y += static_cast<size_t>(subsample)) {
    const int16_t* line = has_chroma
                              ? representation.get_line_chroma(frame_id, y)
                              : representation.get_line(frame_id, y);
    if (!line) continue;

    // PAL V-axis alternation at frame-flat level (ITU-R BT.470-6 §3.5.1).
    // Within each field the V component alternates sign on alternating lines.
    // Frame-flat: determine which field this line belongs to, then apply
    // the same `((line_in_field + field_id) & 1U)` rule as the field-based
    // code.
    bool negate_v = false;
    if ((vp.system == VideoSystem::PAL || vp.system == VideoSystem::PAL_M) &&
        field1_line_count > 0) {
      const uint8_t field_id = (y < field1_line_count) ? 0U : 1U;
      const size_t line_in_field =
          (y < field1_line_count) ? y : (y - field1_line_count);
      negate_v = (((line_in_field + field_id) & 1U) != 0U);
    }

    for (size_t x = x_start; x < x_end; x += static_cast<size_t>(subsample)) {
      const double c = static_cast<double>(line[x]) - blank;

      double sin_ref = 0.0;
      double cos_ref = 0.0;
      if (approx_4fsc) {
        // CVBS_U10_4FSC 4-point quadrature table
        switch (x & 3U) {
          case 0U:
            sin_ref = 1.0;
            cos_ref = 0.0;
            break;
          case 1U:
            sin_ref = 0.0;
            cos_ref = -1.0;
            break;
          case 2U:
            sin_ref = -1.0;
            cos_ref = 0.0;
            break;
          default:
            sin_ref = 0.0;
            cos_ref = 1.0;
            break;
        }
      } else if (phase_step > 0.0) {
        const double phase = (kPi / 2.0) + phase_step * static_cast<double>(x);
        sin_ref = std::sin(phase);
        cos_ref = std::cos(phase);
      } else {
        continue;
      }

      double u = (2.0 * c * sin_ref) / level_range;
      double v = (2.0 * c * cos_ref) / level_range;
      if (negate_v) v = -v;

      const uint8_t fid =
          (field1_line_count > 0 && y >= field1_line_count) ? 1U : 0U;

      UVSample sample;
      sample.u = clamp_normalized(u) * 32767.0;
      sample.v = clamp_normalized(v) * 32767.0;
      sample.field_id = fid;
      data.samples.push_back(sample);
    }
  }

  ORC_LOG_DEBUG(
      "Extracted {} composite vectorscope samples from frame {} ({} area, "
      "subsample={})",
      data.samples.size(), frame_id, active_area_only ? "active" : "full",
      subsample);

  return data;
}

// Register the tool
REGISTER_ANALYSIS_TOOL(VectorscopeAnalysisTool)

// Force linker to include this object file
void force_link_VectorscopeAnalysisTool() {}

}  // namespace orc
