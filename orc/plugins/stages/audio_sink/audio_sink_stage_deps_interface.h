/*
 * File:        audio_sink_stage_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for AudioSinkStage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_AUDIO_SINK_STAGE_DEPS_INTERFACE_H
#define ORC_CORE_AUDIO_SINK_STAGE_DEPS_INTERFACE_H

#include <cstdint>
#include <string>

#include "video_frame_representation.h"

namespace orc {
struct AudioSinkWriteResult {
  bool success{false};
  uint64_t frames_written{0};
  std::string error_message;
};

class IAudioSinkStageDeps {
 public:
  virtual ~IAudioSinkStageDeps() = default;

  virtual AudioSinkWriteResult write_audio_wav(
      const VideoFrameRepresentation* representation,
      const std::string& output_path) = 0;
};
}  // namespace orc

#endif  // ORC_CORE_AUDIO_SINK_STAGE_DEPS_INTERFACE_H
