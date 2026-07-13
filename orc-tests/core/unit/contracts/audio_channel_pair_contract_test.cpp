/*
 * File:        audio_channel_pair_contract_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Contract tests for the SDK audio channel-pair model: default
 *              VFR accessor behaviour, wrapper forwarding of the three pair
 *              accessors, and SMPTE 272M cadence exactness
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <orc/stage/audio_channel_pair.h>
#include <orc/stage/video_frame_representation.h>

#include <memory>
#include <vector>

namespace orc_unit_test {

namespace {

using orc::audio_pair_offset;
using orc::audio_pairs_in_frame;
using orc::AudioChannelPairDescriptor;
using orc::AudioOrigin;
using orc::FrameID;
using orc::FrameIDRange;
using orc::kAudioBitDepth;
using orc::kAudioSampleRateHz;
using orc::kMaxAudioChannelPairs;
using orc::VideoFrameRepresentation;
using orc::VideoFrameRepresentationWrapper;
using orc::VideoSystem;

// Minimal concrete VFR relying on every audio default.
class DefaultAudioSource : public VideoFrameRepresentation {
 public:
  FrameIDRange frame_range() const override { return {FrameID{0}, FrameID{0}}; }
  size_t frame_count() const override { return 1; }
  bool has_frame(FrameID id) const override { return id == FrameID{0}; }
  std::optional<orc::FrameDescriptor> get_frame_descriptor(
      FrameID) const override {
    return std::nullopt;
  }
  const sample_type* get_frame(FrameID) const override { return nullptr; }
  std::vector<sample_type> get_frame_copy(FrameID) const override { return {}; }
};

// Source with two channel pairs returning distinguishable data from every
// accessor so forwarding is observable.
class TwoPairSource : public DefaultAudioSource {
 public:
  size_t audio_channel_pair_count() const override { return 2; }

  std::optional<AudioChannelPairDescriptor> get_audio_channel_pair_descriptor(
      size_t pair) const override {
    if (pair >= 2) return std::nullopt;
    AudioChannelPairDescriptor desc;
    desc.name = pair == 0 ? "Analogue" : "EFM digital audio";
    desc.origin = pair == 0 ? AudioOrigin::ANALOGUE : AudioOrigin::EFM;
    return desc;
  }

  std::vector<int32_t> get_audio_samples(size_t pair,
                                         FrameID id) const override {
    if (pair >= 2 || id != FrameID{0}) return {};
    // 24-bit-range values distinguishable per pair.
    const int32_t base = pair == 0 ? 1000000 : -2000000;
    return std::vector<int32_t>{base, -base, base + 1, -(base + 1)};
  }
};

// Wrapper with no overrides — must forward every audio accessor.
class PassThrough : public VideoFrameRepresentationWrapper {
 public:
  explicit PassThrough(std::shared_ptr<const VideoFrameRepresentation> source)
      : VideoFrameRepresentationWrapper(std::move(source)) {}
};

}  // namespace

// ---------------------------------------------------------------------------
// Model constants
// ---------------------------------------------------------------------------

TEST(AudioChannelPairContractTest, ModelConstants_MatchSmpte272M) {
  EXPECT_EQ(kMaxAudioChannelPairs, 8u);
  EXPECT_EQ(kAudioSampleRateHz, 48000u);
  EXPECT_EQ(kAudioBitDepth, 24u);
}

// ---------------------------------------------------------------------------
// Default accessor behaviour
// ---------------------------------------------------------------------------

TEST(AudioChannelPairContractTest, Defaults_ReportNoAudioOnEveryAccessor) {
  DefaultAudioSource source;

  EXPECT_EQ(source.audio_channel_pair_count(), 0u);
  EXPECT_FALSE(source.has_audio());
  EXPECT_FALSE(source.get_audio_channel_pair_descriptor(0).has_value());
  EXPECT_TRUE(source.get_audio_samples(0, FrameID{0}).empty());
}

TEST(AudioChannelPairContractTest, HasAudio_DerivesFromPairCount) {
  TwoPairSource source;
  EXPECT_TRUE(source.has_audio());
}

TEST(AudioChannelPairContractTest, OutOfRangePair_YieldsEmptyResults) {
  TwoPairSource source;
  EXPECT_FALSE(source.get_audio_channel_pair_descriptor(2).has_value());
  EXPECT_FALSE(source.get_audio_channel_pair_descriptor(kMaxAudioChannelPairs)
                   .has_value());
  EXPECT_TRUE(source.get_audio_samples(2, FrameID{0}).empty());
}

// ---------------------------------------------------------------------------
// Wrapper forwarding
// ---------------------------------------------------------------------------

TEST(AudioChannelPairContractTest, Wrapper_ForwardsAllThreePairAccessors) {
  auto source = std::make_shared<TwoPairSource>();
  PassThrough wrapper(source);

  EXPECT_EQ(wrapper.audio_channel_pair_count(), 2u);
  EXPECT_TRUE(wrapper.has_audio());

  const auto desc0 = wrapper.get_audio_channel_pair_descriptor(0);
  ASSERT_TRUE(desc0.has_value());
  EXPECT_EQ(desc0->name, "Analogue");
  EXPECT_EQ(desc0->origin, AudioOrigin::ANALOGUE);

  const auto desc1 = wrapper.get_audio_channel_pair_descriptor(1);
  ASSERT_TRUE(desc1.has_value());
  EXPECT_EQ(desc1->name, "EFM digital audio");
  EXPECT_EQ(desc1->origin, AudioOrigin::EFM);

  EXPECT_FALSE(wrapper.get_audio_channel_pair_descriptor(2).has_value());

  EXPECT_EQ(wrapper.get_audio_samples(0, FrameID{0}),
            (std::vector<int32_t>{1000000, -1000000, 1000001, -1000001}));
  EXPECT_EQ(wrapper.get_audio_samples(1, FrameID{0}),
            (std::vector<int32_t>{-2000000, 2000000, -1999999, 1999999}));
  EXPECT_TRUE(wrapper.get_audio_samples(2, FrameID{0}).empty());
}

TEST(AudioChannelPairContractTest, Wrapper_ChainedForwardingReachesSource) {
  auto source = std::make_shared<TwoPairSource>();
  auto first = std::make_shared<PassThrough>(source);
  PassThrough second(first);

  EXPECT_EQ(second.audio_channel_pair_count(), 2u);
  EXPECT_EQ(second.get_audio_samples(0, FrameID{0}).size(), 4u);
}

TEST(AudioChannelPairContractTest, Wrapper_WithNullSource_ReturnsDefaults) {
  PassThrough wrapper(nullptr);

  EXPECT_EQ(wrapper.audio_channel_pair_count(), 0u);
  EXPECT_FALSE(wrapper.get_audio_channel_pair_descriptor(0).has_value());
  EXPECT_TRUE(wrapper.get_audio_samples(0, FrameID{0}).empty());
}

// ---------------------------------------------------------------------------
// Cadence exactness (SMPTE 272M-1994 §14.3)
// ---------------------------------------------------------------------------

TEST(AudioChannelPairContractTest, PairOffset_Pal_ConstantWindows) {
  // PAL: 48000 Hz / 25 fps = exactly 1920 pairs per frame, sequence length 1.
  for (uint64_t frame : {0ull, 1ull, 2ull, 100ull, 90000ull}) {
    EXPECT_EQ(audio_pair_offset(frame, VideoSystem::PAL), frame * 1920u)
        << "frame " << frame;
    EXPECT_EQ(audio_pairs_in_frame(frame, VideoSystem::PAL), 1920u)
        << "frame " << frame;
  }
}

TEST(AudioChannelPairContractTest, PairOffset_Ntsc_MatchesSequenceTable) {
  // SMPTE 272M-1994 §14.3 Table 1: cumulative in-sequence offsets.
  const uint64_t expected_offsets[] = {0u, 1602u, 3203u, 4805u, 6406u, 8008u};
  for (uint64_t n = 0; n <= 5; ++n) {
    EXPECT_EQ(audio_pair_offset(n, VideoSystem::NTSC), expected_offsets[n])
        << "frame " << n;
    EXPECT_EQ(audio_pair_offset(n, VideoSystem::PAL_M), expected_offsets[n])
        << "frame " << n;
  }
}

TEST(AudioChannelPairContractTest, PairsInFrame_Ntsc_FollowsCadence) {
  // 1602 when n mod 5 ∈ {0, 2, 4}; 1601 when n mod 5 ∈ {1, 3}.
  for (uint64_t frame = 0; frame < 1000; ++frame) {
    const uint32_t expected =
        (frame % 5 == 1 || frame % 5 == 3) ? 1601u : 1602u;
    ASSERT_EQ(audio_pairs_in_frame(frame, VideoSystem::NTSC), expected)
        << "frame " << frame;
  }
}

TEST(AudioChannelPairContractTest, PairOffset_Ntsc_NoCumulativeDrift) {
  // Every 5-frame audio frame sequence carries exactly 8008 pairs; over N
  // frames the offset must equal round(N × 8008 / 5) — no accumulated error.
  for (uint64_t frame : {5ull, 30000ull, 1000000ull, 12345679ull}) {
    const uint64_t expected = (frame * 8008u + 2u) / 5u;
    EXPECT_EQ(audio_pair_offset(frame, VideoSystem::NTSC), expected)
        << "frame " << frame;
  }
  // Whole sequences are exact multiples of 8008.
  EXPECT_EQ(audio_pair_offset(5u * 1000000u, VideoSystem::NTSC),
            8008ull * 1000000u);
}

TEST(AudioChannelPairContractTest, PairOffset_UnknownSystem_ReturnsZero) {
  EXPECT_EQ(audio_pair_offset(100, VideoSystem::Unknown), 0u);
  EXPECT_EQ(audio_pairs_in_frame(100, VideoSystem::Unknown), 0u);
}

}  // namespace orc_unit_test
