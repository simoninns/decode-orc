/*
 * File:        audio_deemphasis_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for the 50/15 us audio de-emphasis filter (Q-8)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "audio.h"
#include "dec_audiodeemphasis.h"
#include "section.h"

namespace {

// Section geometry: 98 frames x 6 stereo pairs = 588 samples per channel.
constexpr int kFramesPerSection = 98;
constexpr int kPairsPerFrame = 6;
constexpr int kSamplesPerChannel = kFramesPerSection * kPairsPerFrame;  // 588

// Build a complete AudioSection from full per-channel sample streams (588
// each). All error/concealed flags are clear. |preemphasis| sets the CONTROL
// flag.
AudioSection makeSection(const std::vector<int16_t>& left,
                         const std::vector<int16_t>& right, bool preemphasis) {
  AudioSection section;
  section.metadata.setPreemphasis(preemphasis);
  for (int frame = 0; frame < kFramesPerSection; ++frame) {
    std::vector<int16_t> data(12, 0);
    std::vector<uint8_t> zeros(12, 0);
    for (int pair = 0; pair < kPairsPerFrame; ++pair) {
      const int idx = frame * kPairsPerFrame + pair;
      data[pair * 2] = left[idx];
      data[pair * 2 + 1] = right[idx];
    }
    Audio audio;
    audio.setData(data);
    audio.setErrorData(zeros);
    audio.setConcealedData(zeros);
    section.pushFrame(audio);
  }
  return section;
}

// Flatten a section back into a per-channel sample stream (left or right).
std::vector<int16_t> extractChannel(const AudioSection& section, int channel) {
  std::vector<int16_t> out;
  out.reserve(kSamplesPerChannel);
  for (int frame = 0; frame < kFramesPerSection; ++frame) {
    const std::vector<int16_t> data = section.frame(frame).data();
    for (int pair = 0; pair < kPairsPerFrame; ++pair) {
      out.push_back(data[pair * 2 + channel]);
    }
  }
  return out;
}

std::vector<int16_t> constantStream(int16_t value) {
  return std::vector<int16_t>(kSamplesPerChannel, value);
}

}  // namespace

// A section without the pre-emphasis flag must be emitted bit-for-bit
// unchanged.
TEST(AudioDeemphasisTest, PassesThroughWhenNotPreemphasised) {
  AudioDeemphasis filter;
  const std::vector<int16_t> left = constantStream(12345);
  const std::vector<int16_t> right = constantStream(-9876);
  AudioSection section = makeSection(left, right, /*preemphasis=*/false);

  filter.applySection(section);

  EXPECT_EQ(extractChannel(section, 0), left);
  EXPECT_EQ(extractChannel(section, 1), right);
}

// De-emphasis is unity gain at DC: a constant input converges back to itself.
TEST(AudioDeemphasisTest, UnityGainAtDc) {
  AudioDeemphasis filter;
  const int16_t kLevel = 10000;
  AudioSection section =
      makeSection(constantStream(kLevel), constantStream(kLevel), true);

  filter.applySection(section);

  // By the final frame the first-order filter has fully settled to the input.
  const std::vector<int16_t> outLeft = extractChannel(section, 0);
  const std::vector<int16_t> outRight = extractChannel(section, 1);
  EXPECT_NEAR(outLeft.back(), kLevel, 1);
  EXPECT_NEAR(outRight.back(), kLevel, 1);
}

// De-emphasis attenuates the Nyquist frequency to T2/T1 = 15/50 = 0.3.
TEST(AudioDeemphasisTest, AttenuatesNyquistToThreeTenths) {
  AudioDeemphasis filter;
  const int16_t kLevel = 10000;

  // Full-rate alternation +L,-L,... is the Nyquist tone for each channel.
  std::vector<int16_t> left(kSamplesPerChannel);
  std::vector<int16_t> right(kSamplesPerChannel);
  for (int i = 0; i < kSamplesPerChannel; ++i) {
    left[i] = (i % 2 == 0) ? kLevel : -kLevel;
    right[i] = (i % 2 == 0) ? -kLevel : kLevel;
  }
  AudioSection section = makeSection(left, right, true);

  filter.applySection(section);

  // Steady-state magnitude at Nyquist is 0.3 * level (+-1 for rounding).
  const std::vector<int16_t> outLeft = extractChannel(section, 0);
  const int expected = static_cast<int>(std::lround(0.3 * kLevel));
  EXPECT_NEAR(std::abs(outLeft[kSamplesPerChannel - 1]), expected, 1);
  EXPECT_NEAR(std::abs(outLeft[kSamplesPerChannel - 2]), expected, 1);
}

// The first output sample of an impulse pins the leading coefficient b0, which
// for the 44.1 kHz bilinear-transformed 50/15 us network is ~0.42939.
TEST(AudioDeemphasisTest, ImpulseFirstSampleMatchesB0) {
  AudioDeemphasis filter;
  const int16_t kLevel = 10000;
  std::vector<int16_t> left = constantStream(0);
  left[0] = kLevel;
  AudioSection section = makeSection(left, constantStream(0), true);

  filter.applySection(section);

  const std::vector<int16_t> outLeft = extractChannel(section, 0);
  const int expected = static_cast<int>(std::lround(0.42939 * kLevel));
  EXPECT_NEAR(outLeft[0], expected, 1);
}

// Left and right channels are filtered independently (no cross-talk).
TEST(AudioDeemphasisTest, ChannelsFilteredIndependently) {
  AudioDeemphasis filter;
  AudioSection section =
      makeSection(constantStream(8000), constantStream(-4000), true);

  filter.applySection(section);

  EXPECT_NEAR(extractChannel(section, 0).back(), 8000, 1);
  EXPECT_NEAR(extractChannel(section, 1).back(), -4000, 1);
}

// A pass-through (non-preemphasised) section between pre-emphasised sections
// resets the filter state, so the second run starts cleanly from silence and
// reproduces the same impulse response as the first.
TEST(AudioDeemphasisTest, ResetsStateAcrossNonPreemphasisedSection) {
  AudioDeemphasis filter;
  const int16_t kLevel = 10000;

  std::vector<int16_t> impulse = constantStream(0);
  impulse[0] = kLevel;

  AudioSection first = makeSection(impulse, constantStream(0), true);
  filter.applySection(first);
  const int16_t firstOut = extractChannel(first, 0)[0];

  // Non-preemphasised section flushes the filter memory.
  AudioSection gap = makeSection(constantStream(0), constantStream(0), false);
  filter.applySection(gap);

  AudioSection second = makeSection(impulse, constantStream(0), true);
  filter.applySection(second);
  const int16_t secondOut = extractChannel(second, 0)[0];

  EXPECT_EQ(firstOut, secondOut);
}
