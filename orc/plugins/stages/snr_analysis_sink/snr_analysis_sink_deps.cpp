/*
 * File:        snr_analysis_sink_deps.cpp
 * Module:      orc-core
 * Purpose:     SNRAnalysisSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "snr_analysis_sink_deps.h"

#include <cmath>
#include <fstream>
#include <utility>

namespace orc {
void SNRAnalysisSinkStageDeps::init(TriggerProgressCallback progress_callback,
                                    std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

SNRAnalysisComputeResult SNRAnalysisSinkStageDeps::compute_and_analyze(
    VideoFrameRepresentation* representation,
    IObservationContext& observation_context,
    SNRAnalysisComputeOptions options) {
  (void)observation_context;

  if (!representation) {
    return {false, "Input representation is null", {}, 0};
  }

  (void)options.max_frames;
  (void)options.output_path;
  (void)options.write_csv;

  SNRAnalysisComputeResult result;
  result.success = true;
  result.message = "SNR analysis complete";

  const auto frame_rng = representation->frame_range();
  const uint64_t total_frames = frame_rng.count();

  if (total_frames == 0) {
    logger_.warn("SNRAnalysisSinkDeps: No frames available");
    result.total_frames = 0;
    return result;
  }

  logger_.debug(
      "SNRAnalysisSinkDeps: {} total frames (no SNR observer data in VFrameR)",
      total_frames);

  uint64_t frames_processed = 0;
  for (FrameID fid = frame_rng.first; fid <= frame_rng.last; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      logger_.warn("SNRAnalysisSinkDeps: Cancel requested at frame {}", fid);
      result.success = false;
      result.message = "Cancelled by user";
      result.frame_stats.clear();
      result.total_frames = 0;
      return result;
    }

    frames_processed++;
    if (progress_callback_) {
      progress_callback_(frames_processed, total_frames,
                         "Processing frame " + std::to_string(frames_processed));
    }
  }

  result.total_frames = static_cast<int32_t>(total_frames);

  logger_.debug(
      "SNRAnalysisSinkDeps: Processed {} frames, 0 data buckets (no observer)",
      frames_processed);

  return result;
}

bool SNRAnalysisSinkStageDeps::write_csv(
    const std::string& path, const std::vector<FrameSNRStats>& frame_stats) {
  if (frame_stats.empty()) {
    logger_.warn("SNRAnalysisSinkDeps: No data to write");
    return false;
  }

  logger_.debug("SNRAnalysisSinkDeps: Writing CSV to: {}", path);

  std::ofstream csv(path, std::ios::out | std::ios::trunc);
  if (!csv.is_open()) {
    logger_.error("SNRAnalysisSinkDeps: Failed to open file for writing: {}",
                  path);
    return false;
  }

  csv << "frame_number,white_snr_db,black_psnr_db\n";
  size_t rows_written = 0;
  for (const auto& fs : frame_stats) {
    if (fs.has_data) {
      csv << fs.frame_number << ','
          << (fs.has_white_snr ? fs.white_snr : std::nan("")) << ','
          << (fs.has_black_psnr ? fs.black_psnr : std::nan("")) << '\n';
      rows_written++;
    }
  }

  logger_.debug("SNRAnalysisSinkDeps: Successfully wrote {} data rows to: {}",
                rows_written, path);
  return true;
}
}  // namespace orc
