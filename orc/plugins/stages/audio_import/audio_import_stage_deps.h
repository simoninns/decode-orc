/*
 * File:        audio_import_stage_deps.h
 * Module:      orc-stage-plugin-audio_import
 * Purpose:     Production dependencies for AudioImportStage (WAV file
 *              probing and sample reads)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "audio_import_stage_deps_interface.h"

namespace orc {

// Production implementation: walks the RIFF chunk list to validate the
// format ("fmt " chunk: PCM, stereo, 16- or 24-bit) and locate the "data"
// chunk, then serves whole-chunk sample reads from it.
class AudioImportDeps : public IAudioImportDeps {
 public:
  WavProbeResult open(const std::string& wav_path) override;
  std::vector<int16_t> read_all_pairs_16() const override;
  std::vector<int32_t> read_all_pairs_24() const override;

 private:
  std::string wav_path_;
  uint16_t bits_per_sample_ = 0;  // 16 or 24 once open() succeeds
  uint64_t data_offset_ = 0;      // byte offset of the data chunk payload
  uint64_t data_pairs_ = 0;       // stereo pairs in the data chunk
};

}  // namespace orc
