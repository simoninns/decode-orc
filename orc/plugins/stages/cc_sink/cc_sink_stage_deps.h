/*
 * File:        cc_sink_stage_deps.h
 * Module:      orc-core
 * Purpose:     CCSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_CC_SINK_STAGE_DEPS_H
#define ORC_CORE_CC_SINK_STAGE_DEPS_H

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

#include "cc_sink_stage_deps_interface.h"
#include "closed_caption_observer.h"
#include "eia608_decoder.h"
#include "logging.h"

namespace orc {
class CCSinkStageDeps : public ICCSinkStageDeps {
 public:
  CCSinkStageDeps() = default;

  void init(TriggerProgressCallback progress_callback,
            std::atomic<bool>* cancel_requested) override;

  CCExportResult export_cc(VideoFrameRepresentation* representation,
                           IObservationContext& observation_context,
                           CCExportOptions options) override;

 private:
  bool export_scc(const VideoFrameRepresentation* representation,
                  const std::string& output_path, VideoFormat format,
                  const IObservationContext& observation_context,
                  int32_t& cc_frames_exported);

  bool export_plain_text(const VideoFrameRepresentation* representation,
                         const std::string& output_path, VideoFormat format,
                         const IObservationContext& observation_context,
                         int32_t& cc_frames_exported);

  std::string generate_timestamp(int32_t field_index, VideoFormat format) const;
  uint8_t apply_odd_parity(uint8_t byte) const;
  int32_t sanity_check_data(int32_t data_byte) const;
  bool is_control_code(uint8_t byte) const;
  bool is_printable_char(uint8_t byte) const;

  class SpdlogLoggerAdapter {
   public:
    template <typename... Args>
    void trace(const char* fmt, Args&&... args) const {
      ORC_LOG_TRACE(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(const char* fmt, Args&&... args) const {
      ORC_LOG_DEBUG(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(const char* fmt, Args&&... args) const {
      ORC_LOG_INFO(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(const char* fmt, Args&&... args) const {
      ORC_LOG_WARN(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(const char* fmt, Args&&... args) const {
      ORC_LOG_ERROR(fmt, std::forward<Args>(args)...);
    }
  };

  TriggerProgressCallback progress_callback_;
  std::atomic<bool>* cancel_requested_{nullptr};
  ClosedCaptionObserver closed_caption_observer_;
  EIA608Decoder eia608_decoder_;
  SpdlogLoggerAdapter logger_;
};
}  // namespace orc

#endif  // ORC_CORE_CC_SINK_STAGE_DEPS_H
