/*
 * File:        ac3rf_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     AC3RFSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "ac3rf_sink_stage_deps.h"

#include <ac3/Ac3Decoder.h>

#include <algorithm>
#include <fstream>
#include <utility>

#include "logging.h"

// Adapter: forwards ac3rf Logger calls to the orc spdlog logger.
class SpdlogLogger : public Logger {
 public:
  void log(LogPriority priority, LogCategoryFlags,
           const std::string& message) override {
    auto l = orc::get_logger();
    switch (priority) {
      case eDebug:
        l->debug("[ac3rf] {}", message);
        break;
      case eInfo:
        l->info("[ac3rf] {}", message);
        break;
      case eWarn:
        l->warn("[ac3rf] {}", message);
        break;
      default:
        l->error("[ac3rf] {}", message);
        break;
    }
  }
  [[nodiscard]] bool isEnabled(LogPriority priority,
                               LogCategoryFlags) const override {
    auto l = orc::get_logger();
    const spdlog::level::level_enum lvl = l->level();
    if (priority >= eError) return lvl <= spdlog::level::err;
    if (priority >= eWarn) return lvl <= spdlog::level::warn;
    if (priority >= eInfo) return lvl <= spdlog::level::info;
    return lvl <= spdlog::level::debug;
  }
  void sync() override { orc::get_logger()->flush(); }
};

namespace orc {
void AC3RFSinkStageDeps::init(TriggerProgressCallback progress_callback,
                              std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  cancel_requested_ = cancel_requested;
}

AC3RFSinkDecodeResult AC3RFSinkStageDeps::decode_and_write_ac3(
    const VideoFieldRepresentation* representation,
    const std::string& output_path) {
  std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return {false, 0, "Failed to open output file: " + output_path};
  }

  const auto field_range = representation->field_range();
  const FieldID start_field = field_range.start;
  const FieldID end_field = field_range.end;
  const uint64_t total_fields = end_field.value() - start_field.value();

  ORC_LOG_DEBUG("AC3RFSinkDeps: field range [{}, {}), total_fields={}",
                start_field.value(), end_field.value(), total_fields);

  SpdlogLogger ac3_log;
  Ac3Decoder decoder(ac3_log);

  uint64_t frames_written = 0;

  for (FieldID fid = start_field; fid < end_field; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      out.close();
      return {false, 0, "Cancelled by user"};
    }

    auto symbols = representation->get_ac3_symbols(fid);
    auto frames = decoder.decodeSymbols(symbols);
    for (const auto& frame : frames) {
      out.write(reinterpret_cast<const char*>(frame.data()),
                static_cast<std::streamsize>(frame.size()));
      ++frames_written;
    }

    const uint64_t current = fid.value() - start_field.value() + 1;
    if (progress_callback_) {
      progress_callback_(current, total_fields,
                         "Decoding AC3 RF field " + std::to_string(current) +
                             "/" + std::to_string(total_fields));
    }
  }

  out.close();
  ORC_LOG_INFO("AC3RFSinkDeps: {}", decoder.reedSolomonStatistics());
  return {true, frames_written,
          "Success: " + std::to_string(frames_written) + " frames written"};
}
}  // namespace orc
