/*
 * File:        audio_import_stage_deps_interface.h
 * Module:      orc-stage-plugin-audio_import
 * Purpose:     Dependency interface for AudioImportStage (mockable WAV
 *              file probing and sample access seam)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orc {

// Result of probing an external WAV file for import. The file is valid only
// when it is RIFF/WAVE, PCM, stereo, 16- or 24-bit, with a non-zero header
// sample rate. The stage converts the material to the synchronous 48 kHz
// 24-bit channel-pair pipeline form on ingest, so any SoXR-supported header
// rate is acceptable.
struct WavProbeResult {
  bool valid{false};
  std::string error;            // human-readable reason when !valid
  uint32_t sample_rate{0};      // header sample rate in Hz
  uint16_t bits_per_sample{0};  // 16 or 24 when valid
  uint64_t pair_count{0};       // stereo pairs in the data chunk
};

// Dependency seam for AudioImportStage: probes the WAV header and serves the
// data chunk as interleaved stereo samples. Split out so unit tests can mock
// the file access (no filesystem in unit tests).
//
// The whole data chunk is read in one call because ingest conversion
// (widening + SoXR resampling to the synchronous 48 kHz form) operates on
// the complete stream.
//
// Thread safety: open() is called once before the wrapped representation is
// published; the read methods may then be called concurrently from any
// thread.
class IAudioImportDeps {
 public:
  virtual ~IAudioImportDeps() = default;

  // Opens and validates |wav_path|. On success the instance is bound to the
  // file and the read methods serve its data chunk.
  virtual WavProbeResult open(const std::string& wav_path) = 0;

  // Entire data chunk of a 16-bit file as interleaved stereo samples
  // (native 16-bit values, not widened; whole pairs only). Empty when the
  // opened file is not 16-bit.
  virtual std::vector<int16_t> read_all_pairs_16() const = 0;

  // Entire data chunk of a 24-bit file, each 3-byte little-endian sample
  // unpacked and sign-extended into the 24-bit-in-int32 carrier (whole pairs
  // only). Empty when the opened file is not 24-bit.
  virtual std::vector<int32_t> read_all_pairs_24() const = 0;
};

}  // namespace orc
