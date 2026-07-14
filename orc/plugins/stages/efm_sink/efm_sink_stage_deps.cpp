/*
 * File:        efm_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     EFMSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "efm_sink_stage_deps.h"

#include <orc/stage/logging.h>

#include <algorithm>

#include "efm-decode/efm-lib/efm_exception.h"
#include "efm-decode/efm_processor.h"

namespace orc {
void EFMSinkStageDeps::init(TriggerProgressCallback progress_callback,
                            std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

EFMSinkDecodeResult EFMSinkStageDeps::decode_efm(
    const VideoFrameRepresentation* representation,
    const EFMSinkOptions& options) {
  const auto frame_rng = representation->frame_range();
  const FrameID start_fid = frame_rng.first;
  const FrameID end_fid = frame_rng.last;
  const uint64_t total_frames = frame_rng.count();

  ORC_LOG_DEBUG("EFMSinkDeps: Counting EFM t-values across {} frames",
                total_frames);

  uint64_t total_tvalues = 0;
  for (FrameID fid = start_fid; fid <= end_fid; ++fid) {
    total_tvalues += representation->get_efm_sample_count(fid);
  }

  if (total_tvalues == 0) {
    return {false, "Error: EFMSink: no EFM t-values found in frame range"};
  }
  ORC_LOG_DEBUG("EFMSinkDeps: Total EFM t-values: {}", total_tvalues);

  std::vector<uint8_t> efm_buffer;
  efm_buffer.reserve(total_tvalues);

  uint64_t tvalues_accumulated = 0;
  for (FrameID fid = start_fid; fid <= end_fid; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      return {false, "Cancelled by user"};
    }

    auto samples = representation->get_efm_samples(fid);
    efm_buffer.insert(efm_buffer.end(), samples.begin(), samples.end());
    tvalues_accumulated += samples.size();

    const uint64_t frames_done = fid - start_fid + 1;
    if (frames_done % 10 == 0 && progress_callback_) {
      progress_callback_(tvalues_accumulated, total_tvalues,
                         "Buffering EFM: frame " + std::to_string(frames_done) +
                             "/" + std::to_string(total_frames));
    }
  }

  ORC_LOG_DEBUG("EFMSinkDeps: Buffer complete: {} bytes", efm_buffer.size());

  // The EFM decoder throws efm::EfmDecodeError on unrecoverable conditions
  // (invariant violations, corrupt or unsupported data). Catch it here so a
  // bad decode reports a failure instead of terminating the host process.
  try {
    EfmProcessor processor;
    processor.setAudioMode(options.audio_mode);
    processor.setNoTimecodes(options.no_timecodes);
    processor.setAudacityLabels(options.audacity_labels);
    processor.setNoAudioConcealment(options.no_audio_concealment);
    processor.setZeroPad(options.zero_pad);
    processor.setNoWavHeader(options.no_wav_header);
    processor.setOutputMetadata(options.output_metadata);
    processor.setReportOutput(options.report);

    processor.beginStream(options.output_path,
                          static_cast<int64_t>(efm_buffer.size()));

    constexpr size_t CHUNK_SIZE = 1024;
    size_t offset = 0;
    while (offset < efm_buffer.size()) {
      if (cancel_requested_ && cancel_requested_->load()) {
        return {false, "Cancelled by user"};
      }

      const size_t count = std::min(CHUNK_SIZE, efm_buffer.size() - offset);
      processor.pushChunk(
          {efm_buffer.begin() + static_cast<std::ptrdiff_t>(offset),
           efm_buffer.begin() + static_cast<std::ptrdiff_t>(offset + count)});
      offset += count;

      if (progress_callback_ &&
          (offset % (CHUNK_SIZE * 64) == 0 || offset == efm_buffer.size())) {
        progress_callback_(offset, efm_buffer.size(), "Decoding EFM...");
      }
    }

    const bool ok = processor.finishStream();
    if (!ok) {
      std::string reason = processor.lastError();
      if (reason.empty()) {
        reason = "EFM decoding did not complete successfully";
      }
      return {false, "Error: EFMSink: " + reason};
    }
  } catch (const efm::EfmDecodeError& e) {
    ORC_LOG_ERROR("EFMSinkDeps: {}", e.what());
    return {false, std::string("Error: EFMSink: ") + e.what()};
  }

  if (progress_callback_) {
    progress_callback_(total_tvalues, total_tvalues, "Done");
  }

  return {true, "Success: EFM decoded to " + options.output_path};
}
}  // namespace orc
