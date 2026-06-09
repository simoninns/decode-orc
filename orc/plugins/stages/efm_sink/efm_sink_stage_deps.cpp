/*
 * File:        efm_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     EFMSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "efm_sink_stage_deps.h"

#include <algorithm>

#include "efm_processor.h"
#include "logging.h"

namespace orc {
void EFMSinkStageDeps::init(TriggerProgressCallback progress_callback,
                            std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

EFMSinkDecodeResult EFMSinkStageDeps::decode_efm(
    const VideoFieldRepresentation* representation,
    const EFMSinkOptions& options) {
  const auto field_range = representation->field_range();
  const FieldID start_fid = field_range.start;
  const FieldID end_fid = field_range.end;
  const uint64_t total_fields = end_fid.value() - start_fid.value();

  ORC_LOG_DEBUG("EFMSinkDeps: Counting EFM t-values across {} fields",
                total_fields);

  uint64_t total_tvalues = 0;
  for (FieldID fid = start_fid; fid < end_fid; ++fid) {
    total_tvalues += representation->get_efm_sample_count(fid);
  }

  if (total_tvalues == 0) {
    return {false, "Error: EFMSink: no EFM t-values found in field range"};
  }
  ORC_LOG_DEBUG("EFMSinkDeps: Total EFM t-values: {}", total_tvalues);

  std::vector<uint8_t> efm_buffer;
  efm_buffer.reserve(total_tvalues);

  uint64_t tvalues_accumulated = 0;
  for (FieldID fid = start_fid; fid < end_fid; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      return {false, "Cancelled by user"};
    }

    auto samples = representation->get_efm_samples(fid);
    efm_buffer.insert(efm_buffer.end(), samples.begin(), samples.end());
    tvalues_accumulated += samples.size();

    const uint64_t fields_done = fid.value() - start_fid.value() + 1;
    if (fields_done % 10 == 0 && progress_callback_) {
      progress_callback_(tvalues_accumulated, total_tvalues,
                         "Buffering EFM: field " + std::to_string(fields_done) +
                             "/" + std::to_string(total_fields));
    }
  }

  ORC_LOG_DEBUG("EFMSinkDeps: Buffer complete: {} bytes", efm_buffer.size());

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
    return {false,
            "Error: EFMSink: EfmProcessor::finishStream() returned false"};
  }

  if (progress_callback_) {
    progress_callback_(total_tvalues, total_tvalues, "Done");
  }

  return {true, "Success: EFM decoded to " + options.output_path};
}
}  // namespace orc
