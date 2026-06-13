/*
 * File:        cvbs_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     CVBSSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "cvbs_sink_stage_deps.h"

#include <fstream>
#include <utility>

#include "frame_descriptor.h"
#include "logging.h"

namespace orc {

void CVBSSinkStageDeps::init(TriggerProgressCallback progress_callback,
                              std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

CVBSSinkWriteResult CVBSSinkStageDeps::write_cvbs(
    const VideoFrameRepresentation* representation,
    const std::string& output_path) {
  if (!representation) {
    return {false, 0, "Input representation is null"};
  }

  const auto frame_rng = representation->frame_range();
  const uint64_t total_frames = frame_rng.count();

  if (total_frames == 0) {
    return {false, 0, "Input representation contains no frames"};
  }

  std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return {false, 0, "Failed to open output file: " + output_path};
  }

  ORC_LOG_DEBUG("CVBSSinkDeps: Writing {} frames to {}", total_frames,
                output_path);

  uint64_t frames_written = 0;

  for (FrameID fid = frame_rng.first; fid <= frame_rng.last; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      out.close();
      return {false, frames_written, "Cancelled by user"};
    }

    if (!representation->has_frame(fid)) {
      continue;
    }

    auto desc = representation->get_frame_descriptor(fid);
    if (!desc.has_value()) {
      ORC_LOG_WARN("CVBSSinkDeps: No descriptor for frame {}, skipping", fid);
      continue;
    }

    const size_t sample_count = desc->samples_total;
    const int16_t* frame_data = representation->get_frame(fid);

    if (!frame_data || sample_count == 0) {
      ORC_LOG_WARN("CVBSSinkDeps: Empty frame data for frame {}, skipping",
                   fid);
      continue;
    }

    out.write(reinterpret_cast<const char*>(frame_data),
              static_cast<std::streamsize>(sample_count *
                                           sizeof(VideoFrameRepresentation::sample_type)));

    if (!out) {
      return {false, frames_written,
              "Write error at frame " + std::to_string(fid)};
    }

    ++frames_written;

    if (frames_written % 10 == 0 && progress_callback_) {
      progress_callback_(frames_written, total_frames,
                         "Writing CVBS: frame " +
                             std::to_string(frames_written) + "/" +
                             std::to_string(total_frames));
    }
  }

  out.close();

  ORC_LOG_INFO("CVBSSinkDeps: Wrote {} frames ({} requested) to {}",
               frames_written, total_frames, output_path);

  return {true, frames_written,
          "Success: " + std::to_string(frames_written) + " frames written"};
}

}  // namespace orc
