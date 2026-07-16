/*
 * File:        efm_sink_stage_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for EFMSinkStage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_EFM_SINK_STAGE_DEPS_INTERFACE_H
#define ORC_CORE_EFM_SINK_STAGE_DEPS_INTERFACE_H

#include <orc/stage/triggerable_stage.h>
#include <orc/stage/video_frame_representation.h>

#include <atomic>
#include <string>

namespace orc {
struct EFMSinkOptions {
  std::string output_path;
  bool audio_mode{true};
  bool no_timecodes{false};
  bool audacity_labels{false};
  bool no_audio_concealment{false};
  // Q-8: when true, ignore the 50/15 us pre-emphasis flag and skip de-emphasis.
  bool ignore_preemphasis{false};
  bool zero_pad{false};
  bool no_wav_header{false};
  bool output_metadata{false};
  bool report{false};
};

struct EFMSinkDecodeResult {
  bool success{false};
  std::string status_message;
};

class IEFMSinkStageDeps {
 public:
  virtual ~IEFMSinkStageDeps() = default;

  virtual void init(TriggerProgressCallback progress_callback,
                    std::atomic<bool>* cancel_requested) = 0;

  virtual EFMSinkDecodeResult decode_efm(
      const VideoFrameRepresentation* representation,
      const EFMSinkOptions& options) = 0;
};
}  // namespace orc

#endif  // ORC_CORE_EFM_SINK_STAGE_DEPS_INTERFACE_H
