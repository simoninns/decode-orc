/*
 * File:        hackdac_sink_stage_deps.h
 * Module:      orc-core
 * Purpose:     HackdacSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_HACKDAC_SINK_STAGE_DEPS_H
#define ORC_CORE_HACKDAC_SINK_STAGE_DEPS_H

#include "hackdac_sink_stage_deps_interface.h"

namespace orc {
class HackdacSinkStageDeps : public IHackdacSinkStageDeps {
 public:
  HackdacSinkStageDeps() = default;

  void init(TriggerProgressCallback progress_callback,
            std::atomic<bool>* cancel_requested) override;

  HackdacSinkExportResult export_hackdac(
      const VideoFieldRepresentation* representation,
      const HackdacSinkExportOptions& options) override;

 private:
  bool write_report(const std::string& report_path, VideoSystem resolved_system,
                    size_t input_line_width, size_t processed_fields,
                    const std::optional<SourceParameters>& video_params) const;

  static std::string system_to_string(VideoSystem system);
  static int16_t to_signed_sample(uint16_t sample);

  TriggerProgressCallback progress_callback_;
  std::atomic<bool>* cancel_requested_{nullptr};
};
}  // namespace orc

#endif  // ORC_CORE_HACKDAC_SINK_STAGE_DEPS_H
