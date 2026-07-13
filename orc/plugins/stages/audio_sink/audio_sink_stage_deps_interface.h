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

#include <orc/stage/video_frame_representation.h>

#include <cstdint>
#include <string>

namespace orc {
struct AudioSinkWriteResult {
  bool success{false};
  uint64_t frames_written{0};
  std::string error_message;
};

class IAudioSinkStageDeps {
 public:
  virtual ~IAudioSinkStageDeps() = default;

  // Write the audio channel pair |pair| to output_path as a stereo 24-bit
  // signed LE WAV at kAudioSampleRateHz (48000 Hz). Samples are gathered per
  // frame via get_audio_samples(); frames that yield no audio are written as
  // silence sized by audio_pairs_in_frame().
  virtual AudioSinkWriteResult write_audio_wav(
      const VideoFrameRepresentation* representation,
      const std::string& output_path, size_t pair) = 0;
};
}  // namespace orc

#endif  // ORC_CORE_AUDIO_SINK_STAGE_DEPS_INTERFACE_H
