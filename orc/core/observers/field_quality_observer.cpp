/*
 * File:        field_quality_observer.cpp
 * Module:      orc-core
 * Purpose:     Field quality observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include <field_quality_observer.h>
#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/field_id.h>
#include <orc/stage/frame_id.h>
#include <orc/stage/observation/observation_context.h>
#include <orc/stage/video_frame_representation.h>
#include <orc/support/logging.h>

#include <algorithm>
#include <cmath>

namespace orc {

void FieldQualityObserver::process_frame(
    const VideoFrameRepresentation& representation, FrameID frame_id,
    IObservationContext& context) {
  auto vp_opt = representation.get_video_parameters();
  if (!vp_opt.has_value()) {
    // No video parameters — write zeroed quality for both derived fields
    for (size_t field_idx = 0; field_idx < 2; ++field_idx) {
      FieldID derived_fid(frame_id * 2 + field_idx);
      context.set(derived_fid, "disc_quality", "quality_score", 0.0);
      context.set(derived_fid, "disc_quality", "dropout_count", 0);
      context.set(derived_fid, "disc_quality", "phase_valid", false);
    }
    return;
  }
  const auto& vp = vp_opt.value();

  size_t f1_lines = field1_lines(vp.system);

  for (size_t field_idx = 0; field_idx < 2; ++field_idx) {
    FieldID derived_fid(frame_id * 2 + field_idx);
    size_t field_height = (field_idx == 0)
                              ? f1_lines
                              : static_cast<size_t>(vp.frame_height) - f1_lines;

    double quality_score =
        calculate_quality_score(representation, frame_id, field_height, vp);

    // Get dropout hints (frame-level) for diagnostics
    auto dropout_hints = representation.get_dropout_hints(frame_id);

    context.set(derived_fid, "disc_quality", "quality_score", quality_score);
    context.set(derived_fid, "disc_quality", "dropout_count",
                static_cast<int32_t>(dropout_hints.size()));
    context.set(derived_fid, "disc_quality", "phase_valid", true);

    ORC_LOG_DEBUG("FieldQualityObserver: Field {} quality={:.3f} dropouts={}",
                  derived_fid.value(), quality_score, dropout_hints.size());
  }
}

double FieldQualityObserver::calculate_quality_score(
    const VideoFrameRepresentation& representation, FrameID frame_id,
    size_t field_height, const SourceParameters& vp) const {
  double score = 1.0;

  // Factor 1: Dropout density (frame-level dropouts divided across both fields)
  auto dropout_hints = representation.get_dropout_hints(frame_id);
  if (!dropout_hints.empty()) {
    // Total field samples (approximate using nominal line width)
    size_t total_samples =
        static_cast<size_t>(vp.frame_width_nominal) * field_height;

    if (total_samples > 0) {
      size_t total_dropout_samples = 0;
      for (const auto& region : dropout_hints) {
        total_dropout_samples += region.sample_count;
      }

      // Attribute half the frame dropouts to this field
      double dropout_ratio = static_cast<double>(total_dropout_samples) / 2.0 /
                             static_cast<double>(total_samples);

      score *= std::exp(-10.0 * dropout_ratio);
    }
  }

  return std::max(0.0, std::min(1.0, score));
}

}  // namespace orc
