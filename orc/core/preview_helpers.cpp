/*
 * File:        preview_helpers.cpp
 * Module:      orc-core
 * Purpose:     Helper functions for stage preview rendering
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "preview_helpers.h"

#include <cvbs_signal_constants.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "logging.h"

namespace orc {
namespace PreviewHelpers {

StagePreviewCapability make_signal_preview_capability(
    const std::shared_ptr<const VideoFrameRepresentation>& vfr) {
  if (!vfr || vfr->frame_count() == 0) return {};
  auto params = vfr->get_video_parameters();
  if (!params || !params->is_valid()) return {};

  VideoDataType data_type = VideoDataType::CompositePAL;
  if (params->system == VideoSystem::NTSC ||
      params->system == VideoSystem::PAL_M) {
    data_type = VideoDataType::CompositeNTSC;
  }

  uint32_t active_width =
      (params->active_video_end > params->active_video_start)
          ? static_cast<uint32_t>(params->active_video_end -
                                  params->active_video_start)
          : static_cast<uint32_t>(params->frame_width_nominal);
  uint32_t active_height =
      (params->last_active_frame_line > params->first_active_frame_line)
          ? static_cast<uint32_t>(params->last_active_frame_line -
                                  params->first_active_frame_line)
          : static_cast<uint32_t>(params->frame_height);

  double dar_correction = 1.0;
  if (active_width > 0 && active_height > 0) {
    double active_ratio =
        static_cast<double>(active_width) / static_cast<double>(active_height);
    dar_correction = (4.0 / 3.0) / active_ratio;
  }

  StagePreviewCapability cap;
  cap.supported_data_types = {data_type};
  cap.navigation_extent.item_count = vfr->frame_count();
  cap.navigation_extent.item_label = "frame";
  cap.navigation_extent.granularity = 1;
  cap.geometry.active_width = active_width;
  cap.geometry.active_height = active_height;
  cap.geometry.display_aspect_ratio = 4.0 / 3.0;
  cap.geometry.dar_correction_factor = dar_correction;
  return cap;
}

// Scale a 10-bit CVBS_U10_4FSC int16_t sample to an 8-bit grayscale value.
// Clamped mode maps [black_level, white_level] → [0, 255].
// Raw mode maps [sync_tip_level (-300 mV), peak_level (1000 mV)] → [0, 255].
inline uint8_t scale_10bit_to_8bit(int16_t sample, bool apply_level_scaling,
                                   int32_t black_level, int32_t white_level,
                                   int32_t sync_tip_level, int32_t peak_level) {
  if (apply_level_scaling) {
    int32_t range = white_level - black_level;
    if (range <= 0) return 0;
    int32_t scaled =
        ((static_cast<int32_t>(sample) - black_level) * 255) / range;
    return static_cast<uint8_t>(std::max(0, std::min(255, scaled)));
  } else {
    // Raw: map [sync_tip_level, peak_level] → [0, 255] so the full analogue
    // signal range (-300 mV to 1000 mV) spans the display range.
    int32_t range = peak_level - sync_tip_level;
    if (range <= 0) return 0;
    int32_t scaled =
        ((static_cast<int32_t>(sample) - sync_tip_level) * 255) / range;
    return static_cast<uint8_t>(std::max(0, std::min(255, scaled)));
  }
}

std::vector<PreviewOption> get_standard_preview_options(
    const std::shared_ptr<const VideoFrameRepresentation>& representation) {
  std::vector<PreviewOption> options;

  if (!representation) {
    return options;
  }

  auto video_params = representation->get_video_parameters();
  if (!video_params || !video_params->is_valid()) {
    return options;
  }

  size_t frame_count = representation->frame_count();
  if (frame_count == 0) {
    return options;
  }

  uint32_t width = static_cast<uint32_t>(video_params->frame_width_nominal);
  uint32_t height = static_cast<uint32_t>(video_params->frame_height);

  // 1.0 is a neutral fallback; only the computed path reaches stages that
  // properly configure active area parameters.
  double dar_correction = 1.0;
  if (video_params->active_video_start >= 0 &&
      video_params->active_video_end > video_params->active_video_start &&
      video_params->first_active_frame_line >= 0 &&
      video_params->last_active_frame_line >
          video_params->first_active_frame_line) {
    uint32_t active_width = static_cast<uint32_t>(
        video_params->active_video_end - video_params->active_video_start);
    uint32_t active_height =
        static_cast<uint32_t>(video_params->last_active_frame_line -
                              video_params->first_active_frame_line);
    double active_ratio =
        static_cast<double>(active_width) / static_cast<double>(active_height);
    dar_correction = (4.0 / 3.0) / active_ratio;
  }

  options.push_back(PreviewOption{"interlaced_clamped", "Interlaced Clamped",
                                  false, width, height, frame_count,
                                  dar_correction});
  options.push_back(PreviewOption{"interlaced_raw", "Interlaced Raw", false,
                                  width, height, frame_count, dar_correction});
  options.push_back(PreviewOption{"sequential_clamped", "Sequential Clamped",
                                  false, width, height, frame_count,
                                  dar_correction});
  options.push_back(PreviewOption{"sequential_raw", "Sequential Raw", false,
                                  width, height, frame_count, dar_correction});

  return options;
}

PreviewImage render_standard_preview(
    const std::shared_ptr<const VideoFrameRepresentation>& representation,
    const std::string& option_id, uint64_t index, PreviewNavigationHint hint) {
  (void)hint;
  PreviewImage result{};

  if (!representation) {
    return result;
  }

  auto video_params = representation->get_video_parameters();
  if (!video_params || !video_params->is_valid()) {
    return result;
  }

  FrameID frame_id = representation->frame_range().first + index;
  if (!representation->has_frame(frame_id)) {
    return result;
  }

  auto descriptor = representation->get_frame_descriptor(frame_id);
  if (!descriptor) {
    return result;
  }

  const bool apply_level_scaling =
      (option_id == "sequential_clamped" || option_id == "interlaced_clamped");
  const bool do_interlace =
      (option_id == "interlaced_clamped" || option_id == "interlaced_raw");

  if (option_id != "sequential_clamped" && option_id != "sequential_raw" &&
      option_id != "interlaced_clamped" && option_id != "interlaced_raw") {
    ORC_LOG_WARN("PreviewHelpers (frame): Unknown preview option '{}'",
                 option_id);
    return result;
  }

  uint32_t width = static_cast<uint32_t>(descriptor->samples_per_line_nominal);
  uint32_t height = static_cast<uint32_t>(descriptor->height);
  int32_t black_level = video_params->black_level;
  int32_t white_level = video_params->white_level;
  int32_t sync_tip_level = video_params->sync_tip_level;
  int32_t peak_level = video_params->peak_level;

  // Determine field-line count and dominance for interlaced weaving.
  // VFR field 1 is always the top spatial field; all systems use even display
  // rows.
  //   PAL:   field 1 (313 lines, top) → even display rows.
  //   NTSC:  field 1 (263 lines, top) → even display rows.
  //   PAL_M: field 1 (263 lines, top) → even display rows.
  size_t field1_lines = static_cast<size_t>(height) / 2;
  bool field1_on_even_rows = true;
  if (do_interlace) {
    switch (video_params->system) {
      case VideoSystem::PAL:
        field1_lines = static_cast<size_t>(kPalField1Lines);
        field1_on_even_rows = true;
        break;
      case VideoSystem::NTSC:
        field1_lines = static_cast<size_t>(kNtscField1Lines);
        field1_on_even_rows = true;
        break;
      case VideoSystem::PAL_M:
        field1_lines = static_cast<size_t>(kPalMField1Lines);
        field1_on_even_rows = true;
        break;
      default:
        break;
    }
  }

  result.width = width;
  result.height = height;
  result.rgb_data.resize(static_cast<size_t>(width) * height * 3);

  for (uint32_t display_row = 0; display_row < height; ++display_row) {
    size_t buf_line;
    if (do_interlace) {
      const bool use_field1 = (display_row % 2 == 0) == field1_on_even_rows;
      buf_line =
          use_field1 ? (display_row / 2) : (field1_lines + display_row / 2);
      if (buf_line >= static_cast<size_t>(height)) {
        buf_line = static_cast<size_t>(height) - 1;
      }
    } else {
      buf_line = display_row;
    }

    const VideoFrameRepresentation::sample_type* line =
        representation->get_line(frame_id, buf_line);
    if (!line) continue;

    for (uint32_t x = 0; x < width; ++x) {
      uint8_t gray =
          scale_10bit_to_8bit(line[x], apply_level_scaling, black_level,
                              white_level, sync_tip_level, peak_level);
      size_t offset = (static_cast<size_t>(display_row) * width + x) * 3;
      result.rgb_data[offset + 0] = gray;
      result.rgb_data[offset + 1] = gray;
      result.rgb_data[offset + 2] = gray;
    }
  }

  // Convert DropoutRun (frame-flat offsets) to DropoutRegion (display-row
  // coordinates) using the same interlace/sequential layout as above.
  auto dropout_runs = representation->get_dropout_hints(frame_id);
  const size_t spl_nom = static_cast<size_t>(width);
  for (const auto& run : dropout_runs) {
    uint64_t offset = run.sample_start;
    uint32_t remaining = run.sample_count;
    while (remaining > 0) {
      auto [flat_line, sample_in_line] = frame_flat_offset_to_line_sample(
          video_params->system, spl_nom, offset);
      size_t line_len =
          frame_line_sample_count(video_params->system, spl_nom, flat_line);
      uint32_t samples_this_line = static_cast<uint32_t>(
          std::min<uint64_t>(remaining, line_len - sample_in_line));

      int32_t field = 1;
      int32_t line_in_field = static_cast<int32_t>(flat_line);
      switch (video_params->system) {
        case VideoSystem::PAL:
          if (flat_line >= static_cast<size_t>(kPalField1Lines)) {
            field = 2;
            line_in_field = static_cast<int32_t>(flat_line) - kPalField1Lines;
          }
          break;
        case VideoSystem::NTSC:
          if (flat_line >= static_cast<size_t>(kNtscField1Lines)) {
            field = 2;
            line_in_field = static_cast<int32_t>(flat_line) - kNtscField1Lines;
          }
          break;
        case VideoSystem::PAL_M:
          if (flat_line >= static_cast<size_t>(kPalMField1Lines)) {
            field = 2;
            line_in_field = static_cast<int32_t>(flat_line) - kPalMField1Lines;
          }
          break;
        default:
          break;
      }

      uint32_t display_row;
      if (do_interlace) {
        display_row = (field == 1)
                          ? (field1_on_even_rows
                                 ? static_cast<uint32_t>(line_in_field) * 2
                                 : static_cast<uint32_t>(line_in_field) * 2 + 1)
                          : (field1_on_even_rows
                                 ? static_cast<uint32_t>(line_in_field) * 2 + 1
                                 : static_cast<uint32_t>(line_in_field) * 2);
      } else {
        display_row = (field == 1) ? static_cast<uint32_t>(line_in_field)
                                   : static_cast<uint32_t>(field1_lines) +
                                         static_cast<uint32_t>(line_in_field);
      }

      if (display_row < height) {
        DropoutRegion region;
        region.line = display_row;
        region.start_sample = static_cast<uint32_t>(sample_in_line);
        region.end_sample =
            static_cast<uint32_t>(sample_in_line + samples_this_line);
        region.basis = DropoutRegion::DetectionBasis::HINT_DERIVED;
        result.dropout_regions.push_back(region);
      }

      offset += samples_this_line;
      remaining -= samples_this_line;
    }
  }

  return result;
}

}  // namespace PreviewHelpers
}  // namespace orc
