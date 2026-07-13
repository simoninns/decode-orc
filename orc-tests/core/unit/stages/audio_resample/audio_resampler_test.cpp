/*
 * File:        audio_resampler_test.cpp
 * Module:      orc-tests/core/unit/stages/audio_resample
 * Purpose:     Unit tests for the shared AudioResampler library (16→24-bit
 *              widening, any-rate → synchronous 48 kHz conversion, cadence
 *              segmentation)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "audio-resample/audio_resampler.h"

#include <gtest/gtest.h>
#include <orc/stage/audio_channel_pair.h>
#include <orc/stage/common_types.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace orc {
namespace tests {

namespace {

// Interleaved stereo int32 ramp: pair i carries i on both channels, so every
// stream position is uniquely identifiable in segmentation tests.
std::vector<int32_t> make_ramp_pairs(size_t pair_count) {
  std::vector<int32_t> samples;
  samples.reserve(pair_count * 2);
  for (size_t i = 0; i < pair_count; ++i) {
    const int32_t v = static_cast<int32_t>(i);
    samples.push_back(v);
    samples.push_back(v);
  }
  return samples;
}

// A constant-DC stereo signal in the 24-bit-in-int32 carrier.
std::vector<int32_t> make_dc_pairs(size_t pair_count, int32_t level) {
  return std::vector<int32_t>(pair_count * 2, level);
}

}  // namespace

// ===========================================================================
// widen_16_to_24
// ===========================================================================

TEST(AudioResamplerTest, Widen16To24_ShiftsLeftEightSignPreserving) {
  const std::vector<int16_t> input = {0, 1, -1, 1000, -1000, 32767, -32768};
  const auto output = AudioResampler::widen_16_to_24(input);

  ASSERT_EQ(output.size(), input.size());
  EXPECT_EQ(output[0], 0);
  EXPECT_EQ(output[1], 256);
  EXPECT_EQ(output[2], -256);
  EXPECT_EQ(output[3], 256000);
  EXPECT_EQ(output[4], -256000);
  EXPECT_EQ(output[5], 8388352);   // 32767 << 8
  EXPECT_EQ(output[6], -8388608);  // -32768 << 8 = 24-bit minimum

  // Exactly reversible: shifting back recovers the 16-bit values.
  for (size_t i = 0; i < input.size(); ++i) {
    EXPECT_EQ(static_cast<int16_t>(output[i] >> 8), input[i]);
  }
}

TEST(AudioResamplerTest, Widen16To24_EmptyInputReturnsEmpty) {
  EXPECT_TRUE(AudioResampler::widen_16_to_24({}).empty());
}

// ===========================================================================
// resample
// ===========================================================================

TEST(AudioResamplerTest, Resample_EmptyInputReturnsEmpty) {
  EXPECT_TRUE(AudioResampler::resample({}, 44100.0, 48000.0).empty());
}

TEST(AudioResamplerTest, Resample_SameRateIsPassthrough) {
  const auto input = make_ramp_pairs(1000);
  EXPECT_EQ(AudioResampler::resample(input, 48000.0, 48000.0), input);
}

TEST(AudioResamplerTest, Resample_44100To48000PreservesDuration) {
  // One second of 44.1 kHz audio must convert to one second at 48 kHz:
  // output pair count within ±2 of round(n × 48000 / 44100).
  const size_t in_pairs = 44100;
  const auto input = make_dc_pairs(in_pairs, 256000);
  const auto output = AudioResampler::resample(input, 44100.0, 48000.0);

  ASSERT_FALSE(output.empty());
  ASSERT_EQ(output.size() % 2, 0u);
  const auto out_pairs = static_cast<int64_t>(output.size() / 2);
  const auto expected = static_cast<int64_t>(
      std::llround(static_cast<double>(in_pairs) * 48000.0 / 44100.0));
  EXPECT_NEAR(static_cast<double>(out_pairs), static_cast<double>(expected),
              2.0);
}

TEST(AudioResamplerTest, Resample_PreservesDCLevel) {
  // A constant (DC) signal in the 24-bit carrier must stay at the same level
  // through SoXR (linear, unity passband gain).
  const int32_t level = 1000 << 8;  // 256000
  const auto input = make_dc_pairs(44100, level);
  const auto output = AudioResampler::resample(input, 44100.0, 48000.0);

  ASSERT_FALSE(output.empty());
  // Sample away from the filter edges.
  const size_t mid = (output.size() / 4) * 2;
  EXPECT_NEAR(static_cast<double>(output[mid]), static_cast<double>(level),
              512.0);
  EXPECT_NEAR(static_cast<double>(output[mid + 1]), static_cast<double>(level),
              512.0);
}

// ===========================================================================
// resample_to_synchronous
// ===========================================================================

TEST(AudioResamplerTest, ResampleToSynchronous_PAL_IsSampleExactSegmentation) {
  // 48 kHz PAL input needs no resample: pure 1920-pair cadence slicing.
  const auto input = make_ramp_pairs(1920 * 2);
  const auto frames = AudioResampler::resample_to_synchronous(
      input, 48000.0, VideoSystem::PAL, 2);

  ASSERT_EQ(frames.size(), 2u);
  ASSERT_EQ(frames[0].size(), 1920u * 2);
  ASSERT_EQ(frames[1].size(), 1920u * 2);
  EXPECT_EQ(frames[0][0], 0);
  EXPECT_EQ(frames[0][2], 1);
  // Frame 1 starts at pair 1920 exactly.
  EXPECT_EQ(frames[1][0], 1920);
  EXPECT_EQ(frames[1].back(), 1920 * 2 - 1);
}

TEST(AudioResamplerTest, ResampleToSynchronous_PAL_ZeroPadsShortFinalFrame) {
  // 1920 + 100 pairs: frame 1 has 100 real pairs then silence.
  const auto input = make_ramp_pairs(1920 + 100);
  const auto frames = AudioResampler::resample_to_synchronous(
      input, 48000.0, VideoSystem::PAL, 2);

  ASSERT_EQ(frames.size(), 2u);
  ASSERT_EQ(frames[1].size(), 1920u * 2);
  EXPECT_EQ(frames[1][0], 1920);
  EXPECT_EQ(frames[1][99 * 2], 2019);  // last real pair
  EXPECT_EQ(frames[1][100 * 2], 0);    // padding starts at pair 100
  EXPECT_EQ(frames[1].back(), 0);
}

TEST(AudioResamplerTest, ResampleToSynchronous_TruncatesExcessInput) {
  // Three frames of material segmented into two frames: the third frame's
  // pairs are discarded and the blocks total audio_pair_offset(2).
  const auto input = make_ramp_pairs(1920 * 3);
  const auto frames = AudioResampler::resample_to_synchronous(
      input, 48000.0, VideoSystem::PAL, 2);

  ASSERT_EQ(frames.size(), 2u);
  size_t total_pairs = 0;
  for (const auto& f : frames) total_pairs += f.size() / 2;
  EXPECT_EQ(total_pairs, audio_pair_offset(2, VideoSystem::PAL));
  EXPECT_EQ(frames[1].back(), 1920 * 2 - 1);
}

TEST(AudioResamplerTest, ResampleToSynchronous_NTSC_FollowsCadence) {
  // NTSC cadence: 1602/1601/1602/1601/1602 pairs over the 5-frame audio
  // frame sequence (SMPTE 272M-1994 §14.3), totalling exactly
  // audio_pair_offset(5) = 8008 pairs, each block starting at its
  // cumulative offset.
  const auto input = make_ramp_pairs(8008);
  const auto frames = AudioResampler::resample_to_synchronous(
      input, 48000.0, VideoSystem::NTSC, 5);

  ASSERT_EQ(frames.size(), 5u);
  size_t total_pairs = 0;
  for (uint64_t i = 0; i < 5; ++i) {
    const auto expected_pairs = audio_pairs_in_frame(i, VideoSystem::NTSC);
    ASSERT_EQ(frames[i].size(), static_cast<size_t>(expected_pairs) * 2)
        << "frame " << i;
    EXPECT_EQ(frames[i][0],
              static_cast<int32_t>(audio_pair_offset(i, VideoSystem::NTSC)))
        << "frame " << i;
    total_pairs += frames[i].size() / 2;
  }
  EXPECT_EQ(frames[0].size(), 1602u * 2);
  EXPECT_EQ(frames[1].size(), 1601u * 2);
  EXPECT_EQ(total_pairs, audio_pair_offset(5, VideoSystem::NTSC));
  EXPECT_EQ(frames[4].back(), 8007);
}

TEST(AudioResamplerTest, ResampleToSynchronous_ResamplesNonSynchronousInput) {
  // 44.1 kHz DC input for two PAL frames: blocks are cadence-sized and stay
  // near the DC level after resampling.
  const int32_t level = 256000;
  const auto input = make_dc_pairs(44100, level);  // one second of material
  const auto frames = AudioResampler::resample_to_synchronous(
      input, 44100.0, VideoSystem::PAL, 2);

  ASSERT_EQ(frames.size(), 2u);
  ASSERT_EQ(frames[0].size(), 1920u * 2);
  ASSERT_EQ(frames[1].size(), 1920u * 2);
  // Sample away from the filter edges (middle of frame 1).
  EXPECT_NEAR(static_cast<double>(frames[1][1920]), static_cast<double>(level),
              512.0);
}

TEST(AudioResamplerTest, ResampleToSynchronous_EmptyInputYieldsSilentFrames) {
  const auto frames = AudioResampler::resample_to_synchronous(
      {}, 48000.0, VideoSystem::NTSC, 3);

  ASSERT_EQ(frames.size(), 3u);
  for (uint64_t i = 0; i < 3; ++i) {
    ASSERT_EQ(
        frames[i].size(),
        static_cast<size_t>(audio_pairs_in_frame(i, VideoSystem::NTSC)) * 2);
    EXPECT_EQ(frames[i][0], 0);
    EXPECT_EQ(frames[i].back(), 0);
  }
}

TEST(AudioResamplerTest, ResampleToSynchronous_UnknownSystemYieldsEmptyFrames) {
  const auto input = make_ramp_pairs(1000);
  const auto frames = AudioResampler::resample_to_synchronous(
      input, 48000.0, VideoSystem::Unknown, 2);

  ASSERT_EQ(frames.size(), 2u);
  EXPECT_TRUE(frames[0].empty());
  EXPECT_TRUE(frames[1].empty());
}

// ===========================================================================
// SDK cadence helpers used by the resampler contract
// ===========================================================================

TEST(AudioChannelPairHelpersTest, PairsPerFrame_MatchSmpte272MCadence) {
  EXPECT_EQ(audio_pairs_in_frame(0, VideoSystem::PAL), 1920u);
  EXPECT_EQ(audio_pairs_in_frame(1, VideoSystem::PAL), 1920u);
  // SMPTE 272M-1994 §14.3 Table 1: 1602/1601/1602/1601/1602.
  EXPECT_EQ(audio_pairs_in_frame(0, VideoSystem::NTSC), 1602u);
  EXPECT_EQ(audio_pairs_in_frame(1, VideoSystem::NTSC), 1601u);
  EXPECT_EQ(audio_pairs_in_frame(2, VideoSystem::NTSC), 1602u);
  EXPECT_EQ(audio_pairs_in_frame(3, VideoSystem::NTSC), 1601u);
  EXPECT_EQ(audio_pairs_in_frame(4, VideoSystem::NTSC), 1602u);
  EXPECT_EQ(audio_pairs_in_frame(2, VideoSystem::PAL_M), 1602u);
  EXPECT_EQ(audio_pairs_in_frame(0, VideoSystem::Unknown), 0u);
}

}  // namespace tests
}  // namespace orc
