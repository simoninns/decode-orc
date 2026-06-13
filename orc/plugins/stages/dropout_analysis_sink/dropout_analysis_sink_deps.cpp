/*
 * File:        dropout_analysis_sink_deps.cpp
 * Module:      orc-core
 * Purpose:     DropoutAnalysisSinkStage dependency implementation (VFrameR)
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
    VideoFrameRepresentation* representation,
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

  auto range = representation->frame_range();
  if (range.count() == 0) {
    logger_.warn("DropoutAnalysisSinkDeps: No frames available");
    result.total_frames = 0;
    return result;
  }

  const size_t total_frames_count = static_cast<size_t>(range.count());
  const auto active_hint = representation->get_active_line_hint();
  const auto video_params = representation->get_video_parameters();

  // Nominal samples per line for line-position approximation in visible-area
  // mode. PAL: 1135, NTSC: 910.
  int32_t nominal_spl = 910;
  if (video_params) nominal_spl = video_params->frame_width_nominal;

  std::map<int32_t, FrameDropoutStats> frame_data;

  for (size_t i = 0; i < total_frames_count; ++i) {
    if (cancel_requested_ && cancel_requested_->load()) {
      logger_.warn("DropoutAnalysisSinkDeps: Cancel requested at frame {}", i);
      result.success = false;
      result.message = "Cancelled by user";
      result.frame_stats.clear();
      result.total_frames = 0;
      return result;
    }

    const FrameID fid = range.first + i;
    const auto desc = representation->get_frame_descriptor(fid);
    if (!desc) continue;

    const auto runs = representation->get_dropout_hints(fid);

    double frame_dropout_length = 0.0;
    size_t frame_dropout_count = 0;

    for (const auto& run : runs) {
      bool include = true;

      if (options.mode == DropoutAnalysisMode::VISIBLE_AREA) {
        // Approximate frame-flat line of this run's start position.
        const int32_t approx_line =
            static_cast<int32_t>(run.sample_start / static_cast<uint64_t>(nominal_spl));
        const int32_t approx_sample =
            static_cast<int32_t>(run.sample_start % static_cast<uint64_t>(nominal_spl));

        // Filter by active frame line range.
        if (active_hint) {
          if (approx_line < active_hint->first_active_frame_line ||
              approx_line > active_hint->last_active_frame_line) {
            include = false;
          }
        }

        // Filter by active video sample range within line.
        if (include && video_params &&
            video_params->active_video_start >= 0 &&
            video_params->active_video_end >= 0) {
          if (approx_sample >= video_params->active_video_end ||
              approx_sample + static_cast<int32_t>(run.sample_count) <=
                  video_params->active_video_start) {
            include = false;
          }
        }
      }

      if (include) {
        uint64_t length = run.sample_count;

        // Clamp to active_video_start / active_video_end within the line.
        if (options.mode == DropoutAnalysisMode::VISIBLE_AREA && video_params &&
            video_params->active_video_start >= 0 &&
            video_params->active_video_end >= 0) {
          const int32_t approx_sample =
              static_cast<int32_t>(run.sample_start % static_cast<uint64_t>(nominal_spl));
          const int32_t clamped_start =
              std::max(approx_sample, video_params->active_video_start);
          const int32_t clamped_end =
              std::min(approx_sample + static_cast<int32_t>(run.sample_count),
                       video_params->active_video_end);
          length = static_cast<uint64_t>(
              std::max(0, clamped_end - clamped_start));
        }

        frame_dropout_length += static_cast<double>(length);
        frame_dropout_count++;
      }
    }

    const int32_t frame_num = static_cast<int32_t>(fid) + 1;

    auto& accum = frame_data[frame_num];
    accum.frame_number = frame_num;
    accum.total_dropout_length += frame_dropout_length;
    accum.dropout_count += static_cast<double>(frame_dropout_count);
    if (frame_dropout_count > 0) accum.has_data = true;

    if (progress_callback_) {
      progress_callback_(i + 1, total_frames_count,
                         "Processing frame " + std::to_string(i));
    }
  }

  if (frame_data.empty()) {
    logger_.warn("DropoutAnalysisSinkDeps: No frame data accumulated");
    result.total_frames = 0;
    return result;
  }

  const size_t total_frames = frame_data.size();
  result.total_frames = static_cast<int32_t>(total_frames);

  const size_t TARGET_DATA_POINTS = 1000;
  size_t frames_per_bin = std::max<size_t>(
      1, (total_frames + TARGET_DATA_POINTS - 1) / TARGET_DATA_POINTS);

  logger_.debug(
      "DropoutAnalysisSinkDeps: {} total frames, {} frames per bin",
      total_frames, frames_per_bin);

  FrameDropoutStats current_bin{};
  size_t frames_in_bin = 0;
  [[maybe_unused]] int32_t bin_start_frame = 0;

  for (const auto& [frame_number, accum] : frame_data) {
    if (frames_in_bin == 0) {
      bin_start_frame = frame_number;
    }

    current_bin.frame_number = frame_number;
    current_bin.total_dropout_length += accum.total_dropout_length;
    current_bin.dropout_count += accum.dropout_count;
    current_bin.has_data = current_bin.has_data || accum.has_data;

    frames_in_bin++;

    if (frames_in_bin >= frames_per_bin) {
      result.frame_stats.push_back(current_bin);
      current_bin = FrameDropoutStats{};
      frames_in_bin = 0;
    }
  }

  if (frames_in_bin > 0) {
    result.frame_stats.push_back(current_bin);
  }

  logger_.debug(
      "DropoutAnalysisSinkDeps: {} data buckets from {} total frames",
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
    logger_.error("DropoutAnalysisSinkDeps: Failed to open file: {}", path);
    return false;
  }

  csv << "frame_number,total_dropout_length_samples,total_dropout_count\n";
  size_t rows = 0;
  for (const auto& fs : frame_stats) {
    if (fs.has_data) {
      csv << fs.frame_number << ',' << fs.total_dropout_length << ','
          << fs.dropout_count << '\n';
      rows++;
    }
  }

  logger_.debug("DropoutAnalysisSinkDeps: Wrote {} rows to: {}", rows, path);
  return true;
}

}  // namespace orc
