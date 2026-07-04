/*
 * File:        free_running_resampler.h
 * Module:      orc-stage-plugin-audio-sink
 * Purpose:     SoXR-based resampler from frame-locked to free-running audio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <vector>

namespace orc {

// ---------------------------------------------------------------------------
// FreeRunningAudioResampler
// ---------------------------------------------------------------------------
// Converts stereo PCM from the NTSC/PAL-M frame-locked rate of
// 44100000/1001 Hz ≈ 44055.944 Hz back to free-running 44100 Hz for standard
// WAV export.  PAL audio is already at 44100 Hz and never passes through this
// class.
//
// Counterpart of the tbc_source plugin's NtscPalMAudioResampler (which
// converts the raw 44100 Hz capture down to the locked rate); duplicated here
// because stage plugins cannot link against each other.
//
// Thread-safety: stateless static method — safe to call from any thread.
class FreeRunningAudioResampler {
 public:
  // Resample interleaved stereo int16_t PCM from in_rate to out_rate Hz using
  // SoXR HQ resampling.  Returns interleaved stereo int16_t at out_rate.
  //
  // Returns empty vector on error (e.g. SoXR allocation failure).
  static std::vector<int16_t> resample(const std::vector<int16_t>& input_stereo,
                                       double in_rate, double out_rate);
};

}  // namespace orc
