/*
 * File:        burst_level_observer.cpp
 * Module:      orc-core
 * Purpose:     Color burst median IRE level observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "burst_level_observer.h"

#include <cvbs_signal_constants.h>

#include <algorithm>
#include <cmath>
#include <numeric>

#include "../include/field_id.h"
#include "../include/logging.h"
#include "../include/observation_context.h"
#include "../include/video_frame_representation.h"

namespace orc {

void BurstLevelObserver::process_frame(
    const VideoFrameRepresentation& representation, FrameID frame_id,
    IObservationContext& context) {
  auto vp_opt = representation.get_video_parameters();
  if (!vp_opt.has_value()) {
    ORC_LOG_TRACE("BurstLevelObserver: No video parameters for frame {}",
                  frame_id);
    return;
  }
  const auto& vp = vp_opt.value();

  // Colour burst range derived from video system constant.
  const auto [sys_burst_start, sys_burst_end] = colour_burst_range(vp.system);
  size_t burst_start_idx = static_cast<size_t>(sys_burst_start);
  size_t burst_end_idx = static_cast<size_t>(sys_burst_end);

  size_t f1_lines = field1_lines(vp.system);
  size_t line_width = static_cast<size_t>(vp.frame_width_nominal);

  // CVBS_U10_4FSC: threshold for outlier rejection in 10-bit sample domain.
  // 30 IRE → 30.0 / ire_per_unit where ire_per_unit = 100 / (white - blanking).
  const double outlier_threshold_10bit =
      30.0 * static_cast<double>(vp.white_level - vp.blanking_level) / 100.0;

  for (size_t field_idx = 0; field_idx < 2; ++field_idx) {
    FieldID derived_fid(frame_id * 2 + field_idx);
    size_t line_offset = (field_idx == 0) ? 0 : f1_lines;
    size_t field_height = (field_idx == 0)
                              ? f1_lines
                              : static_cast<size_t>(vp.frame_height) - f1_lines;

    // Collect all burst samples from sampled lines
    std::vector<double> burst_levels_raw;

    // Sample from line 11 to end of active area
    size_t start_line = 11;
    size_t last_active_field_line =
        static_cast<size_t>(vp.last_active_frame_line / 2);
    size_t end_line = std::min(field_height - 10, last_active_field_line > 0
                                                      ? last_active_field_line
                                                      : field_height - 10);

    if (end_line <= start_line) {
      continue;
    }

    // Sample just 3 lines (top, middle, bottom) for performance
    std::vector<size_t> sample_lines = {
        start_line, start_line + (end_line - start_line) / 2, end_line - 1};

    for (size_t field_line : sample_lines) {
      const int16_t* line_data =
          representation.get_line(frame_id, line_offset + field_line);
      if (!line_data) {
        continue;
      }

      size_t burst_end = burst_end_idx;
      if (burst_end >= line_width) {
        burst_end = line_width - 1;
      }
      if (burst_end <= burst_start_idx) {
        continue;
      }

      // Collect raw samples from this line's burst region
      std::vector<double> line_burst_samples;
      for (size_t sample_idx = burst_start_idx; sample_idx <= burst_end;
           ++sample_idx) {
        line_burst_samples.push_back(
            static_cast<double>(line_data[sample_idx]));
      }

      if (line_burst_samples.size() < 4) {
        continue;
      }

      // Calculate mean of burst samples
      double mean = std::accumulate(line_burst_samples.begin(),
                                    line_burst_samples.end(), 0.0) /
                    static_cast<double>(line_burst_samples.size());

      // Subtract mean (remove DC component)
      std::vector<double> centered;
      for (double sample : line_burst_samples) {
        centered.push_back(sample - mean);
      }

      // Calculate RMS
      double sum_squares = 0.0;
      for (double val : centered) {
        sum_squares += val * val;
      }
      double rms =
          std::sqrt(sum_squares / static_cast<double>(centered.size()));

      // Convert RMS to peak amplitude: peak = RMS * sqrt(2)
      double peak_amplitude = rms * std::sqrt(2.0);

      // Skip outliers (> 30 IRE equivalent in 10-bit domain)
      if (peak_amplitude > outlier_threshold_10bit) {
        continue;
      }

      burst_levels_raw.push_back(peak_amplitude);
    }

    if (burst_levels_raw.empty()) {
      ORC_LOG_TRACE("BurstLevelObserver: No valid burst samples for field {}",
                    derived_fid.value());
      continue;
    }

    double median_raw = calculate_median(burst_levels_raw);

    context.set(derived_fid, "burst_level", "median_burst_10bit", median_raw);

    ORC_LOG_DEBUG("BurstLevelObserver: Field {} median_burst_10bit={:.2f}",
                  derived_fid.value(), median_raw);
  }
}

double BurstLevelObserver::calculate_median(std::vector<double> values) const {
  if (values.empty()) {
    return 0.0;
  }

  std::sort(values.begin(), values.end());

  size_t n = values.size();
  if (n % 2 == 0) {
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
  } else {
    return values[n / 2];
  }
}

}  // namespace orc
