/*
 * File:        dec_audiodeemphasis.cpp
 * Purpose:     efm-decoder-audio - 50/15 us audio de-emphasis (Q-8)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_audiodeemphasis.h"

#include <cmath>
#include <vector>

#include "../efm-lib/efm_constants.h"

namespace {

// IEC 60908 §17.5 CONTROL: pre-emphasis is the 50/15 us network. The playback
// (de-emphasis) response is the inverse analogue transfer function
//   H(s) = (1 + s*T2) / (1 + s*T1),   T1 = 50 us, T2 = 15 us,
// which is unity at DC and attenuates to T2/T1 = 0.3 (-10.5 dB) at high
// frequency. Discretising with the bilinear transform s =
// 2*fs*(1-z^-1)/(1+z^-1) at the CD sample rate gives a first-order IIR
//   y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1].
constexpr double kCdSampleRateHz = 44100.0;
constexpr double kTau1Seconds = 50.0e-6;  // pre-emphasis pole time constant
constexpr double kTau2Seconds = 15.0e-6;  // pre-emphasis zero time constant

// Bilinear-transform pre-warp factor K = 2*fs and the two warped time
// constants; all coefficients follow directly from these.
constexpr double kK = 2.0 * kCdSampleRateHz;
constexpr double kKTau1 = kK * kTau1Seconds;
constexpr double kKTau2 = kK * kTau2Seconds;

constexpr double kB0 = (1.0 + kKTau2) / (1.0 + kKTau1);
constexpr double kB1 = (1.0 - kKTau2) / (1.0 + kKTau1);
constexpr double kA1 = (1.0 - kKTau1) / (1.0 + kKTau1);

// Section/frame geometry: 98 frames of 12 interleaved L,R int16 samples.
constexpr int kFramesPerSection = efm::kFramesPerSection;
constexpr int kSamplesPerFrame = 12;

// int16_t saturation bounds for the filtered output.
constexpr double kInt16Min = -32768.0;
constexpr double kInt16Max = 32767.0;

}  // namespace

AudioDeemphasis::AudioDeemphasis()
    : m_previousSectionPreemphasised(false),
      m_deemphasisedSectionCount(0),
      m_passThroughSectionCount(0) {}

int16_t AudioDeemphasis::filterSample(BiquadState& state,
                                      int16_t sample) const {
  const double x0 = static_cast<double>(sample);
  const double y0 = kB0 * x0 + kB1 * state.x1 - kA1 * state.y1;
  state.x1 = x0;
  state.y1 = y0;

  // De-emphasis has unity DC gain and attenuates elsewhere, so it cannot
  // amplify a full-scale input; clamp defensively against rounding all the
  // same.
  double rounded = std::round(y0);
  if (rounded > kInt16Max) rounded = kInt16Max;
  if (rounded < kInt16Min) rounded = kInt16Min;
  return static_cast<int16_t>(rounded);
}

void AudioDeemphasis::resetState() {
  m_left = BiquadState{};
  m_right = BiquadState{};
}

void AudioDeemphasis::applySection(AudioSection& section) {
  if (!section.metadata.hasPreemphasis()) {
    // No pre-emphasis on this section: pass it through unchanged. Reset the
    // filter so the next pre-emphasised run starts from silence rather than
    // continuing stale state across the gap.
    if (m_previousSectionPreemphasised) resetState();
    m_previousSectionPreemphasised = false;
    ++m_passThroughSectionCount;
    return;
  }

  // Entering (or continuing) a pre-emphasised run. Reset at the boundary so a
  // track that switches pre-emphasis on does not inherit the previous run's
  // filter memory.
  if (!m_previousSectionPreemphasised) resetState();
  m_previousSectionPreemphasised = true;
  ++m_deemphasisedSectionCount;

  for (int frameIndex = 0; frameIndex < kFramesPerSection; ++frameIndex) {
    const Audio& inFrame = section.frame(frameIndex);
    std::vector<int16_t> data = inFrame.data();  // 12 interleaved L,R samples
    for (int i = 0; i < kSamplesPerFrame; i += 2) {
      data[i] = filterSample(m_left, data[i]);           // left
      data[i + 1] = filterSample(m_right, data[i + 1]);  // right
    }

    // Preserve the frame's error/concealed flags; only the samples change.
    Audio outFrame = inFrame;
    outFrame.setData(data);
    section.setFrame(frameIndex, outFrame);
  }
}

void AudioDeemphasis::showStatistics() const {
  ORC_LOG_INFO("Audio de-emphasis statistics (50/15 us):");
  ORC_LOG_INFO("  De-emphasised sections: {}", m_deemphasisedSectionCount);
  ORC_LOG_INFO("  Pass-through sections:  {}", m_passThroughSectionCount);
}
