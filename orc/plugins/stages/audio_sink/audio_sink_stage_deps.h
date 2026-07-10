/*
 * File:        audio_sink_stage_deps.h
 * Module:      orc-core
 * Purpose:     AudioSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_AUDIO_SINK_STAGE_DEPS_H
#define ORC_CORE_AUDIO_SINK_STAGE_DEPS_H

#include <orc/plugin/orc_stage_services.h>
#include <orc/stage/triggerable_stage.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "audio_sink_stage_deps_interface.h"

namespace orc {
class AudioSinkStageDeps : public IAudioSinkStageDeps {
 public:
  // stage_services may be null (e.g. direct in-process construction in
  // tests); write_audio_wav() then fails with a diagnostic.
  explicit AudioSinkStageDeps(IStageServices* stage_services)
      : stage_services_(stage_services) {}

  void init(TriggerProgressCallback progress_callback,
            std::atomic<bool>* is_processing,
            std::atomic<bool>* cancel_requested);

  AudioSinkWriteResult write_audio_wav(
      const VideoFrameRepresentation* representation,
      const std::string& output_path, size_t track,
      AudioSinkSampleRateMode sample_rate_mode) override;

 private:
  std::vector<uint8_t> build_wav_header(uint32_t num_samples,
                                        uint32_t sample_rate,
                                        uint16_t num_channels,
                                        uint16_t bits_per_sample) const;

  // Streams the frame-locked samples to the WAV file unmodified, declaring
  // header_rate in the header.
  AudioSinkWriteResult write_locked(
      const VideoFrameRepresentation* representation, size_t track,
      FrameIDRange frame_rng, IFileWriterInt16* writer, uint32_t header_rate,
      uint64_t total_samples);

  // Collects the frame-locked samples, resamples them from the NTSC/PAL-M
  // locked rate to free-running 44100 Hz, and writes the result.
  AudioSinkWriteResult write_resampled_to_free_running(
      const VideoFrameRepresentation* representation, size_t track,
      FrameIDRange frame_rng, IFileWriterInt16* writer);

  // Streams a free-running track verbatim at 44100 Hz via the stream
  // accessors (no resampling).
  AudioSinkWriteResult write_stream(
      const VideoFrameRepresentation* representation, size_t track,
      IFileWriterInt16* writer);

  IStageServices* stage_services_{nullptr};
  TriggerProgressCallback progress_callback_;
  std::atomic<bool>* is_processing_{nullptr};
  std::atomic<bool>* cancel_requested_{nullptr};
};
}  // namespace orc

#endif  // ORC_CORE_AUDIO_SINK_STAGE_DEPS_H
