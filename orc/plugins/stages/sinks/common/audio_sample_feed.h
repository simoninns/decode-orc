/*
 * File:        audio_sample_feed.h
 * Module:      orc-core
 * Purpose:     Pure conversions from the 24-bit-in-int32 pipeline audio
 *              carrier to the video sink encoders' sample formats
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_AUDIO_SAMPLE_FEED_H
#define ORC_CORE_AUDIO_SAMPLE_FEED_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace orc {

// SMPTE 272M-1994 §1.3: pipeline audio carries 24-bit two's-complement
// values (−8388608 … 8388607) in int32_t.
inline constexpr int32_t kAudioCarrierMin = -8388608;
inline constexpr int32_t kAudioCarrierMax = 8388607;

// Float feed (AAC FLTP/FLT): full-scale 24-bit maps to ±1.0.
inline float audio_carrier_to_float(int32_t carrier) {
  return static_cast<float>(carrier) / 8388608.0f;
}

// S32 feed (FLAC 24-bit, PCM_S24LE): FFmpeg carries 24-bit samples in the
// top 3 bytes of S32.
inline int32_t audio_carrier_to_s32(int32_t carrier) { return carrier << 8; }

// S16 feed: drop the low 8 bits (exact inverse of the << 8 ingest widening
// for 16-bit source material).
inline int16_t audio_carrier_to_s16(int32_t carrier) {
  return static_cast<int16_t>(carrier >> 8);
}

// Apply a linear gain in the 24-bit carrier domain, saturating at full
// scale. Silence stays silence, so gain-adjusted padding is fine too.
inline int32_t audio_apply_gain(int32_t carrier, double gain) {
  const double scaled = static_cast<double>(carrier) * gain;
  return static_cast<int32_t>(
      std::lround(std::clamp(scaled, static_cast<double>(kAudioCarrierMin),
                             static_cast<double>(kAudioCarrierMax))));
}

}  // namespace orc

#endif  // ORC_CORE_AUDIO_SAMPLE_FEED_H
