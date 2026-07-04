/*
 * File:        free_running_resampler.cpp
 * Module:      orc-stage-plugin-audio-sink
 * Purpose:     SoXR-based resampler from frame-locked to free-running audio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "free_running_resampler.h"

#include <orc/stage/logging.h>
#include <soxr.h>

#include <cmath>

namespace orc {

std::vector<int16_t> FreeRunningAudioResampler::resample(
    const std::vector<int16_t>& input_stereo, double in_rate, double out_rate) {
  if (input_stereo.empty()) return {};

  constexpr unsigned kChannels = 2;
  const size_t in_frames = input_stereo.size() / kChannels;
  if (in_frames == 0) return {};

  // Estimate output frame count with a small safety margin.
  const size_t out_estimate =
      static_cast<size_t>(
          std::lround(static_cast<double>(in_frames) * out_rate / in_rate)) +
      static_cast<size_t>(kChannels) * 16;

  std::vector<int16_t> output((out_estimate + 64) * kChannels, 0);

  // SoXR HQ quality, int16_t interleaved I/O.
  // SOXR_INT16_I = signed 16-bit interleaved (all channels in one array).
  const soxr_io_spec_t io_spec = soxr_io_spec(SOXR_INT16_I, SOXR_INT16_I);
  const soxr_quality_spec_t quality = soxr_quality_spec(SOXR_HQ, 0);

  soxr_error_t err = nullptr;
  soxr_t soxr = soxr_create(in_rate, out_rate, kChannels, &err, &io_spec,
                            &quality, nullptr);
  if (!soxr || err) {
    ORC_LOG_ERROR("FreeRunningAudioResampler: soxr_create failed: {}",
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
    ORC_LOG_WARN("FreeRunningAudioResampler: soxr_process error: {}", err);
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

}  // namespace orc
