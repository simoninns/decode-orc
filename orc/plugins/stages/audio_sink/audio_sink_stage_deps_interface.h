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

// Output sample-rate policy for the WAV file (frame-locked tracks only).
//
// Frame-locked pipeline audio: PAL carries 44100 Hz (25 fps × 1764 pairs
// per frame), NTSC/PAL-M carries 44100000/1001 Hz ≈ 44055.94 Hz (30000/1001
// fps × 1470 pairs per frame). For PAL the locked rate equals the standard
// rate, so both modes produce identical output. Free-running tracks are
// already at 44100 Hz and are written verbatim; the mode is ignored.
enum class AudioSinkSampleRateMode {
  // Write the frame-locked samples unmodified; for NTSC/PAL-M the WAV header
  // declares the locked rate (44056 Hz).
  kLocked,
  // Resample NTSC/PAL-M locked audio to free-running 44100 Hz.
  kFreeRunning,
};

class IAudioSinkStageDeps {
 public:
  virtual ~IAudioSinkStageDeps() = default;

  // Write pipeline audio track |track| to output_path. Frame-locked tracks
  // are gathered per frame; free-running tracks are streamed verbatim at
  // 44100 Hz through the stream accessors.
  virtual AudioSinkWriteResult write_audio_wav(
      const VideoFrameRepresentation* representation,
      const std::string& output_path, size_t track,
      AudioSinkSampleRateMode sample_rate_mode) = 0;
};
}  // namespace orc

#endif  // ORC_CORE_AUDIO_SINK_STAGE_DEPS_INTERFACE_H
