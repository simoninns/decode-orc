/*
 * File:        audio_resampler.cpp
 * Module:      orc-audio-resample (shared stage-plugin library)
 * Purpose:     SoXR-based stereo conversion of any-rate audio to the
 *              synchronous 48 kHz 24-bit channel-pair pipeline form
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "audio_resampler.h"

#include <orc/stage/logging.h>
#include <soxr.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orc {

// ---------------------------------------------------------------------------
// widen_16_to_24
// ---------------------------------------------------------------------------

std::vector<int32_t> AudioResampler::widen_16_to_24(
    const std::vector<int16_t>& input) {
  std::vector<int32_t> output(input.size());
  std::transform(input.begin(), input.end(), output.begin(),
                 [](int16_t s) { return static_cast<int32_t>(s) << 8; });
  return output;
}

// ---------------------------------------------------------------------------
// resample
// ---------------------------------------------------------------------------

std::vector<int32_t> AudioResampler::resample(
    const std::vector<int32_t>& input_stereo, double in_rate, double out_rate) {
  if (input_stereo.empty()) return {};
  if (in_rate == out_rate) return input_stereo;

  constexpr unsigned kChannels = 2;
  const size_t in_frames = input_stereo.size() / kChannels;
  if (in_frames == 0) return {};

  // Estimate output frame count with a small safety margin.
  const size_t out_estimate =
      static_cast<size_t>(
          std::lround(static_cast<double>(in_frames) * out_rate / in_rate)) +
      static_cast<size_t>(kChannels) * 16;

  std::vector<int32_t> output((out_estimate + 64) * kChannels, 0);

  // SoXR HQ quality, int32 interleaved I/O.
  // SOXR_INT32_I = signed 32-bit interleaved (all channels in one array).
  // The 24-bit-in-int32 carrier passes through unchanged in scale: SoXR is
  // linear, so 24-bit-ranged input yields 24-bit-ranged output.
  const soxr_io_spec_t io_spec = soxr_io_spec(SOXR_INT32_I, SOXR_INT32_I);
  const soxr_quality_spec_t quality = soxr_quality_spec(SOXR_HQ, 0);

  soxr_error_t err = nullptr;
  soxr_t soxr = soxr_create(in_rate, out_rate, kChannels, &err, &io_spec,
                            &quality, nullptr);
  if (!soxr || err) {
    ORC_LOG_ERROR("AudioResampler: soxr_create failed: {}",
                  err ? err : "null handle");
    if (soxr) soxr_delete(soxr);
    return {};
  }

  size_t idone = 0;
  size_t odone = 0;

  // Process input.
  err = soxr_process(soxr, input_stereo.data(), in_frames, &idone,
                     output.data(), out_estimate, &odone);
  if (err) {
    ORC_LOG_WARN("AudioResampler: soxr_process error: {}", err);
    soxr_delete(soxr);
    output.resize(odone * kChannels);
    return output;
  }

  // Flush residual samples (nullptr input signals end-of-stream to SoXR).
  const size_t headroom = (output.size() / kChannels) - odone;
  size_t odone2 = 0;
  soxr_process(soxr, nullptr, 0, nullptr, output.data() + odone * kChannels,
               headroom, &odone2);
  odone += odone2;

  soxr_delete(soxr);
  output.resize(odone * kChannels);
  return output;
}

// ---------------------------------------------------------------------------
// resample_to_synchronous
// ---------------------------------------------------------------------------

std::vector<std::vector<int32_t>> AudioResampler::resample_to_synchronous(
    const std::vector<int32_t>& raw_stereo, double in_rate_hz,
    VideoSystem system, size_t frame_count) {
  std::vector<std::vector<int32_t>> frames(frame_count);
  if (system == VideoSystem::Unknown) return frames;

  if (raw_stereo.empty() || frame_count == 0) {
    for (size_t i = 0; i < frame_count; ++i) {
      frames[i].assign(static_cast<size_t>(audio_pairs_in_frame(i, system)) * 2,
                       0);
    }
    return frames;
  }

  // Convert to the synchronous 48 kHz rate (no-op for 48 kHz input).
  std::vector<int32_t> resampled;
  const std::vector<int32_t>* stream = &raw_stereo;
  if (in_rate_hz != static_cast<double>(kAudioSampleRateHz)) {
    resampled = resample(raw_stereo, in_rate_hz,
                         static_cast<double>(kAudioSampleRateHz));
    stream = &resampled;
  }

  // Segment into cadence-sized blocks; zero-pad short material and truncate
  // excess so the blocks total exactly audio_pair_offset(frame_count) pairs.
  for (size_t i = 0; i < frame_count; ++i) {
    const size_t block_pairs =
        static_cast<size_t>(audio_pairs_in_frame(i, system));
    const size_t src_start =
        static_cast<size_t>(audio_pair_offset(i, system)) * 2;
    const size_t src_end = src_start + block_pairs * 2;

    frames[i].assign(block_pairs * 2, 0);

    if (src_start < stream->size()) {
      const size_t available = std::min(src_end, stream->size()) - src_start;
      std::memcpy(frames[i].data(), stream->data() + src_start,
                  available * sizeof(int32_t));
      // Remaining values are already zero from assign().
    }
    // When src_start >= stream->size() the frame is silent (all zeros).
  }

  return frames;
}

}  // namespace orc
