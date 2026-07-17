/*
 * File:        white_flag_observer.cpp
 * Module:      orc-core
 * Purpose:     White flag observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/field_id.h>
#include <orc/stage/observation/white_flag_observer.h>
#include <orc/stage/video_frame_representation.h>
#include <orc/support/logging.h>

namespace orc {

void WhiteFlagObserver::process_frame(
    const VideoFrameRepresentation& representation, FrameID frame_id,
    IObservationContext& context) {
  auto vp_opt = representation.get_video_parameters();
  if (!vp_opt.has_value()) {
    ORC_LOG_TRACE("WhiteFlagObserver: No video parameters for frame {}",
                  frame_id);
    return;
  }
  const auto& vp = vp_opt.value();

  // White flag is NTSC-only
  if (vp.system != VideoSystem::NTSC) {
    return;
  }

  // CVBS_U10_4FSC zero-crossing: midpoint between blanking and white.
  int16_t zero_crossing =
      static_cast<int16_t>((vp.white_level + vp.blanking_level) / 2);

  size_t line_width = static_cast<size_t>(vp.frame_width_nominal);
  size_t f1_lines = field1_lines(vp.system);

  for (size_t field_idx = 0; field_idx < 2; ++field_idx) {
    FieldID derived_fid(frame_id * 2 + field_idx);
    size_t line_offset = (field_idx == 0) ? 0 : f1_lines;
    size_t field_height = (field_idx == 0)
                              ? f1_lines
                              : static_cast<size_t>(vp.frame_height) - f1_lines;

    // Line 11 (0-based index 10)
    constexpr size_t line_num = 10;
    if (line_num >= field_height) {
      continue;
    }

    const int16_t* line_data =
        representation.get_line(frame_id, line_offset + line_num);
    if (!line_data) {
      continue;
    }

    size_t active_start = line_width / 8;
    size_t active_end = line_width * 7 / 8;
    if (active_end <= active_start) {
      continue;
    }

    size_t white_count = 0;
    size_t total_count = active_end - active_start;
    for (size_t i = active_start; i < active_end; ++i) {
      if (line_data[i] > zero_crossing) {
        white_count++;
      }
    }

    bool present = (white_count > total_count / 2);
    context.set(derived_fid, "white_flag", "present", present);

    ORC_LOG_DEBUG(
        "WhiteFlagObserver: Field {} white_flag={} (white {}/{} samples)",
        derived_fid.value(), present, white_count, total_count);
  }
}

}  // namespace orc
