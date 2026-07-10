/*
 * File:        audio_track.h
 * Module:      decode-orc Plugin SDK (stage contract)
 * Purpose:     Audio track model shared by all VideoFrameRepresentation
 *              implementations (descriptors, track limits, stream timing)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <orc/stage/common_types.h>

#include <cstdint>
#include <numeric>
#include <string>

namespace orc {

// ============================================================================
// Audio track model
// ============================================================================
// Every audio track in the pipeline is stereo, interleaved int16_t pairs
// (L, R, L, R, …) — the only encoding permitted by the CVBS file format
// specification. A track is either frame-locked (exact integer stereo pairs
// per video frame, addressed per-FrameID) or free-running (a stream
// independent of frame timing, addressed by stereo-pair offset; pair 0 is
// synchronous with the first sample of the representation's first frame).

// Maximum number of stereo audio tracks a representation may carry — the CVBS
// container limit. Enforced at every track-adding stage, not just at the
// container sink, so a DAG that validates also exports.
inline constexpr size_t kMaxAudioTracks = 16;

// Exact rational sample rate in Hz (e.g. 44100/1, 44100000/1001).
struct AudioSampleRate {
  uint32_t num;
  uint32_t den;
};

// Where a track's audio came from.
enum class AudioTrackOrigin : uint8_t {
  ANALOGUE,  // decoder-provided analogue audio (LD analogue, tape linear)
  HIFI,      // tape Hi-Fi stereo
  EFM,       // decoded EFM digital audio
  IMPORTED,  // attached external WAV
  DERIVED,   // produced by a transform (e.g. bilingual split)
  UNKNOWN,
};

struct AudioTrackDescriptor {
  std::string name;  // human-readable, e.g. "Analogue", "EFM digital"
  AudioTrackOrigin origin = AudioTrackOrigin::UNKNOWN;
  bool locked = false;          // frame-locked vs free-running
  AudioSampleRate sample_rate;  // authoritative rate; WAV headers approximate
};

// The standard free-running audio rate (CVBS spec / CD audio): 44100 Hz.
inline constexpr AudioSampleRate kFreeRunningAudioRate{44100, 1};

// The frame-locked audio rate for a video system:
//   PAL          — 44100/1 Hz     (1764 stereo pairs per frame)
//   NTSC / PAL-M — 44100000/1001 Hz (1470 stereo pairs per frame)
// Unknown systems have no defined locked rate; 0/1 is returned.
inline constexpr AudioSampleRate locked_audio_sample_rate(VideoSystem system) {
  switch (system) {
    case VideoSystem::PAL:
      return {44100, 1};
    case VideoSystem::NTSC:
    case VideoSystem::PAL_M:
      return {44100000, 1001};
    default:
      return {0, 1};
  }
}

// Stream pair offset corresponding to the START of frame |frame_index| for a
// free-running track, computed with exact 64-bit rational arithmetic:
//   round(frame_index × rate / frame_rate)
// where frame_rate is 25/1 (PAL) or 30000/1001 (NTSC, PAL-M).
//
// Every stage and sink that maps frames to free-running stream positions MUST
// use this helper so window boundaries agree pipeline-wide. Use the
// cumulative form — per-frame pair counts are NOT constant (a 44100 Hz NTSC
// stream yields alternating 1471/1472-pair windows).
//
// Returns 0 for VideoSystem::Unknown or a zero-valued rate component.
inline uint64_t audio_stream_pair_offset(uint64_t frame_index,
                                         AudioSampleRate rate,
                                         VideoSystem system) {
  uint64_t frame_rate_num = 0;
  uint64_t frame_rate_den = 1;
  switch (system) {
    case VideoSystem::PAL:
      // ITU-R BT.1700 Annex 1 Part B (625-line PAL): 25 frames/s.
      frame_rate_num = 25;
      frame_rate_den = 1;
      break;
    case VideoSystem::NTSC:
    case VideoSystem::PAL_M:
      // SMPTE 170M-2004 Section 11.3: 30000/1001 frames/s.
      frame_rate_num = 30000;
      frame_rate_den = 1001;
      break;
    default:
      return 0;
  }
  if (rate.num == 0 || rate.den == 0) return 0;

  // offset = round(frame_index × (rate.num/rate.den) / (fr_num/fr_den)).
  // Reduce num/den before multiplying by frame_index so the intermediate
  // product stays within 64 bits for any realistic frame index (audio rates
  // reduce to at most six digits against video frame rates).
  uint64_t num = static_cast<uint64_t>(rate.num) * frame_rate_den;
  uint64_t den = static_cast<uint64_t>(rate.den) * frame_rate_num;
  const uint64_t g = std::gcd(num, den);
  num /= g;
  den /= g;
  return (frame_index * num + den / 2) / den;
}

}  // namespace orc
