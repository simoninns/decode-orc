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

#include <atomic>
#include <cstdint>
#include <fstream>

#include "audio_sink_stage_deps_interface.h"
#include "triggerable_stage.h"

namespace orc {
class AudioSinkStageDeps : public IAudioSinkStageDeps {
 public:
  AudioSinkStageDeps() = default;

  void init(TriggerProgressCallback progress_callback,
            std::atomic<bool>* is_processing,
            std::atomic<bool>* cancel_requested);

  AudioSinkWriteResult write_audio_wav(
      const VideoFrameRepresentation* representation,
      const std::string& output_path) override;

 private:
  bool write_wav_header(std::ofstream& out, uint32_t num_samples,
                        uint32_t sample_rate, uint16_t num_channels,
                        uint16_t bits_per_sample) const;

  TriggerProgressCallback progress_callback_;
  std::atomic<bool>* is_processing_{nullptr};
  std::atomic<bool>* cancel_requested_{nullptr};
};
}  // namespace orc

#endif  // ORC_CORE_AUDIO_SINK_STAGE_DEPS_H
