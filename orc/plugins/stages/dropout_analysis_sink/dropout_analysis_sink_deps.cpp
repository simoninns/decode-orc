/*
 * File:        dropout_analysis_sink_deps.cpp
 * Module:      orc-core
 * Purpose:     DropoutAnalysisSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "dropout_analysis_sink_deps.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

namespace orc {
void DropoutAnalysisSinkStageDeps::init(
    TriggerProgressCallback progress_callback,
    std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

DropoutAnalysisComputeResult DropoutAnalysisSinkStageDeps::compute_and_analyze(
    VideoFieldRepresentation* representation,
    IObservationContext& observation_context,
    DropoutAnalysisComputeOptions options) {
  (void)observation_context;
  (void)options.max_frames;

  if (!representation) {
    return {false, "Input representation is null", {}, 0};
  }

  DropoutAnalysisComputeResult result;
  result.success = true;
  result.message = "Dropout analysis complete";

  auto range = representation->field_range();
  if (range.size() == 0) {
    logger_.warn("DropoutAnalysisSinkDeps: No fields available");
    result.total_frames = 0;
    return result;
  }

  const size_t total_fields = range.size();
  const auto active_hint = representation->get_active_line_hint();
  const auto video_params = representation->get_video_parameters();

  struct FrameAccumulation {
    double total_dropout_length = 0.0;
    double dropout_count = 0.0;
    bool has_data = false;
  };

  std::map<int32_t, FrameAccumulation> frame_accum;

  for (size_t i = 0; i < total_fields; ++i) {
    if (cancel_requested_ && cancel_requested_->load()) {
      logger_.warn("DropoutAnalysisSinkDeps: Cancel requested at field {}", i);
      result.success = false;
      result.message = "Cancelled by user";
      result.frame_stats.clear();
      result.total_frames = 0;
      return result;
    }

    FieldID fid(range.start.value() + i);
    const auto field_descriptor = representation->get_descriptor(fid);
    if (!field_descriptor) {
      continue;
    }

    const auto dropouts = representation->get_dropout_hints(fid);

    double field_dropout_length = 0.0;
    size_t field_dropout_count = 0;

    for (const auto& dropout : dropouts) {
      bool include = true;

      if (options.mode == DropoutAnalysisMode::VISIBLE_AREA) {
        if (active_hint) {
          if (static_cast<int32_t>(dropout.line) <
                  active_hint->first_active_field_line ||
              static_cast<int32_t>(dropout.line) >
                  active_hint->last_active_field_line) {
            include = false;
          }
        }

        if (include && video_params && video_params->active_video_start >= 0 &&
            video_params->active_video_end >= 0) {
          if (static_cast<int32_t>(dropout.end_sample) <=
                  video_params->active_video_start ||
              static_cast<int32_t>(dropout.start_sample) >=
                  video_params->active_video_end) {
            include = false;
          }
        }
      }

      if (include) {
        uint32_t start = dropout.start_sample;
        uint32_t end = dropout.end_sample;

        if (options.mode == DropoutAnalysisMode::VISIBLE_AREA && video_params &&
            video_params->active_video_start >= 0 &&
            video_params->active_video_end >= 0) {
          start = std::max(
              start, static_cast<uint32_t>(video_params->active_video_start));
          end = std::min(end,
                         static_cast<uint32_t>(video_params->active_video_end));
        }

        field_dropout_length += static_cast<double>(end - start);
        field_dropout_count++;
      }
    }

    const int32_t frame_num = field_descriptor->frame_number.value_or(
        static_cast<int32_t>((fid.value() / 2) + 1));

    auto& accum = frame_accum[frame_num];
    accum.total_dropout_length += field_dropout_length;
    accum.dropout_count += static_cast<double>(field_dropout_count);
    if (field_dropout_count > 0) {
      accum.has_data = true;
    }

    if (progress_callback_) {
      progress_callback_(i + 1, total_fields,
                         "Processing field " + std::to_string(i));
    }
  }

  if (frame_accum.empty()) {
    logger_.warn("DropoutAnalysisSinkDeps: No frame data accumulated");
    result.total_frames = 0;
    return result;
  }

  const size_t total_frames = frame_accum.size();
  result.total_frames = static_cast<int32_t>(total_frames);

  const size_t TARGET_DATA_POINTS = 1000;
  size_t fields_per_bin = 2;
  if (total_fields > TARGET_DATA_POINTS * 2) {
    fields_per_bin =
        (total_fields + TARGET_DATA_POINTS - 1) / TARGET_DATA_POINTS;
    if (fields_per_bin % 2 != 0) {
      fields_per_bin++;
    }
  }

  size_t frames_per_bin = std::max<size_t>(1, fields_per_bin / 2);

  logger_.debug(
      "DropoutAnalysisSinkDeps: {} total fields ({} total frames), binning by "
      "{} fields per data point",
      total_fields, total_frames, fields_per_bin);

  FrameDropoutStats current_bin{};
  size_t frames_in_bin = 0;
  [[maybe_unused]] int32_t bin_start_frame = 0;

  for (const auto& [frame_number, accum] : frame_accum) {
    if (frames_in_bin == 0) {
      bin_start_frame = frame_number;
    }

    current_bin.frame_number = frame_number;
    current_bin.total_dropout_length += accum.total_dropout_length;
    current_bin.dropout_count += accum.dropout_count;
    current_bin.has_data = current_bin.has_data || accum.has_data;

    frames_in_bin++;

    if (frames_in_bin >= frames_per_bin) {
      logger_.debug(
          "DropoutAnalysisSinkDeps: Bucket {} - frames {}-{}: "
          "total_dropout_length={:.2f}, dropout_count={:.2f} ({} frames)",
          result.frame_stats.size(), bin_start_frame, frame_number,
          current_bin.total_dropout_length, current_bin.dropout_count,
          frames_in_bin);

      result.frame_stats.push_back(current_bin);
      current_bin = FrameDropoutStats();
      frames_in_bin = 0;
    }
  }

  if (frames_in_bin > 0) {
    logger_.debug(
        "DropoutAnalysisSinkDeps: Final bucket {} - frames {}-{}: "
        "total_dropout_length={:.2f}, dropout_count={:.2f} ({} frames)",
        result.frame_stats.size(), bin_start_frame, current_bin.frame_number,
        current_bin.total_dropout_length, current_bin.dropout_count,
        frames_in_bin);
    result.frame_stats.push_back(current_bin);
  }

  logger_.debug(
      "DropoutAnalysisSinkDeps: Computed {} data buckets from {} total frames",
      result.frame_stats.size(), total_frames);

  return result;
}

bool DropoutAnalysisSinkStageDeps::write_csv(
    const std::string& path,
    const std::vector<FrameDropoutStats>& frame_stats) {
  if (frame_stats.empty()) {
    logger_.warn("DropoutAnalysisSinkDeps: No data to write");
    return false;
  }

  logger_.debug("DropoutAnalysisSinkDeps: Writing CSV to: {}", path);

  std::ofstream csv(path, std::ios::out | std::ios::trunc);
  if (!csv.is_open()) {
    logger_.error(
        "DropoutAnalysisSinkDeps: Failed to open file for writing: {}", path);
    return false;
  }

  csv << "frame_number,total_dropout_length_samples,total_dropout_count\n";
  size_t rows_written = 0;
  for (const auto& fs : frame_stats) {
    if (fs.has_data) {
      csv << fs.frame_number << ',' << fs.total_dropout_length << ','
          << fs.dropout_count << '\n';
      rows_written++;
    }
  }

  logger_.debug(
      "DropoutAnalysisSinkDeps: Successfully wrote {} data rows to: {}",
      rows_written, path);
  return true;
}
}  // namespace orc
