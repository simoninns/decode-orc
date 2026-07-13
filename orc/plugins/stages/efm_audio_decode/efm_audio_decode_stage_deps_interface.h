/*
 * File:        efm_audio_decode_stage_deps_interface.h
 * Module:      orc-stage-plugin-efm_audio_decode
 * Purpose:     Dependency interface for EFMAudioDecodeStage (mockable decode
 *              and cache-file access seam)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <orc/stage/video_frame_representation.h>

#include <cstdint>
#include <string>
#include <vector>

namespace orc {

// Decode options forwarded to the shared EFM decode pipeline (a subset of
// efm_sink's audio-mode options).
struct EFMAudioDecodeOptions {
  bool no_timecodes{false};
  bool no_audio_concealment{false};
  // When non-empty, a detailed decode statistics report is written to this
  // path. Empty (the default) means no report is written. Unlike efm_sink,
  // this stage has no user-facing audio output file to append ".txt" to, so
  // the report location is specified explicitly.
  std::string report_path;
  // Human-readable name for the appended EFM audio channel pair. Surfaces as
  // the pair descriptor name (the CVBS container description column and the
  // video sink's per-stream title). Empty falls back to "EFM digital audio".
  std::string pair_name{"EFM digital audio"};
};

struct EFMAudioDecodeResult {
  bool success{false};
  std::string error_message;      // human-readable reason when !success
  uint64_t stream_pair_count{0};  // decoded 44.1 kHz stereo pairs available
};

// Dependency seam for EFMAudioDecodeStage: performs the whole-stream EFM
// decode into a scratch cache, serves stereo-pair reads from it, and holds
// the converted synchronous (48 kHz 24-bit-in-int32, cadence-aligned)
// pipeline stream. Split out so unit tests can mock the decode and file
// access (no filesystem in unit tests).
//
// Thread safety: decode_to_cache(), read_cache_pairs(0, …) for the raw
// conversion read, and write_synchronous_cache() are called at most once per
// instance (the caller serialises via std::call_once);
// read_synchronous_pairs() may be called from any thread after
// write_synchronous_cache() has returned successfully.
class IEFMAudioDecodeDeps {
 public:
  virtual ~IEFMAudioDecodeDeps() = default;

  // Accumulates the representation's EFM t-value stream across its full frame
  // range, decodes it to headerless 44.1 kHz interleaved stereo int16_t PCM
  // in a scratch cache file, and reports the decoded pair count.
  virtual EFMAudioDecodeResult decode_to_cache(
      const VideoFrameRepresentation& representation,
      const EFMAudioDecodeOptions& options) = 0;

  // Reads up to |pair_count| stereo pairs starting at |first_pair| from the
  // raw 44.1 kHz cache written by decode_to_cache(). Returns interleaved L,R
  // samples; short reads are truncated to whole pairs. Empty when no cache
  // exists.
  virtual std::vector<int16_t> read_cache_pairs(uint64_t first_pair,
                                                uint32_t pair_count) const = 0;

  // Stores the converted pipeline stream — 48 kHz synchronous,
  // 24-bit-in-int32, interleaved stereo, cadence-aligned so stereo pair
  // audio_pair_offset(id, system) starts frame |id| — in the scratch cache,
  // replacing the raw decoded cache. Returns false on I/O failure.
  virtual bool write_synchronous_cache(const std::vector<int32_t>& samples) = 0;

  // Reads up to |pair_count| stereo pairs starting at |first_pair| from the
  // synchronous cache written by write_synchronous_cache(). Returns
  // interleaved L,R int32 values; short reads are truncated to whole pairs.
  // Empty when no cache exists.
  virtual std::vector<int32_t> read_synchronous_pairs(
      uint64_t first_pair, uint32_t pair_count) const = 0;
};

}  // namespace orc
