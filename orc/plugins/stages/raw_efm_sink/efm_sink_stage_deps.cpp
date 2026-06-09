/*
 * File:        efm_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     RawEFMSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "efm_sink_stage_deps.h"

#include <cstddef>

#include "buffered_file_io.h"
#include "logging.h"

namespace orc {
void RawEFMSinkStageDeps::init(TriggerProgressCallback progress_callback,
                               std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

RawEFMSinkWriteResult RawEFMSinkStageDeps::write_raw_efm(
    const VideoFieldRepresentation* representation,
    const std::string& output_path) {
  auto field_range = representation->field_range();
  FieldID start_field = field_range.start;
  FieldID end_field = field_range.end;

  uint64_t total_fields = end_field.value() - start_field.value();
  ORC_LOG_DEBUG("RawEFMSinkDeps: Processing {} fields", total_fields);

  uint64_t total_tvalues = 0;
  for (FieldID fid = start_field; fid < end_field; ++fid) {
    total_tvalues += representation->get_efm_sample_count(fid);
  }

  ORC_LOG_DEBUG("RawEFMSinkDeps: Total EFM t-values: {}", total_tvalues);

  if (total_tvalues == 0) {
    return {false, 0, "Error: No EFM t-values found in field range"};
  }

  BufferedFileWriter<uint8_t> writer(static_cast<size_t>(4 * 1024 * 1024));
  if (!writer.open(output_path)) {
    return {false, 0, "Error: Failed to open output file: " + output_path};
  }

  uint64_t tvalues_written = 0;
  uint64_t invalid_tvalue_count = 0;

  for (FieldID fid = start_field; fid < end_field; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      writer.close();
      return {false, 0, "Cancelled by user"};
    }

    auto tvalues = representation->get_efm_samples(fid);
    if (!tvalues.empty()) {
      for (const auto& tval : tvalues) {
        if (tval < 3 || tval > 11) {
          invalid_tvalue_count++;
        }
      }

      writer.write(tvalues);
      tvalues_written += tvalues.size();
    }

    uint64_t current_field = fid.value() - start_field.value();
    if (current_field % 10 == 0 && progress_callback_) {
      progress_callback_(current_field, total_fields,
                         "Writing EFM field " + std::to_string(current_field) +
                             "/" + std::to_string(total_fields));
    }
  }

  writer.close();

  ORC_LOG_INFO("RawEFMSinkDeps: Successfully wrote {} t-values to {}",
               tvalues_written, output_path);
  ORC_LOG_DEBUG(
      "RawEFMSinkDeps: Expected t-values: {}, Actual t-values: {}, Match: {}",
      total_tvalues, tvalues_written,
      total_tvalues == tvalues_written ? "YES" : "NO");

  if (invalid_tvalue_count > 0) {
    ORC_LOG_WARN(
        "RawEFMSinkDeps: Found {} invalid t-values (outside range [3, 11])",
        invalid_tvalue_count);
  }

  return {true, tvalues_written,
          "Success: " + std::to_string(tvalues_written) + " t-values written"};
}
}  // namespace orc
