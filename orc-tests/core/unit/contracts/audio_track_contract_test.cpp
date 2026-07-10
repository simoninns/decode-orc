/*
 * File:        audio_track_contract_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Contract tests for the SDK audio track model: default VFR
 *              accessor behaviour, wrapper forwarding of all six track
 *              accessors, and audio_stream_pair_offset() exactness
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <orc/stage/audio_track.h>
#include <orc/stage/video_frame_representation.h>

#include <memory>
#include <vector>

namespace orc_unit_test {

namespace {

using orc::audio_stream_pair_offset;
using orc::AudioSampleRate;
using orc::AudioTrackDescriptor;
using orc::AudioTrackOrigin;
using orc::FrameID;
using orc::FrameIDRange;
using orc::kFreeRunningAudioRate;
using orc::locked_audio_sample_rate;
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

// Source with two tracks: locked track 0 and free-running track 1, returning
// distinguishable data from every accessor so forwarding is observable.
class TwoTrackSource : public DefaultAudioSource {
 public:
  size_t audio_track_count() const override { return 2; }

  std::optional<AudioTrackDescriptor> get_audio_track_descriptor(
      size_t track) const override {
    if (track >= 2) return std::nullopt;
    AudioTrackDescriptor desc;
    desc.name = track == 0 ? "Analogue" : "EFM digital audio";
    desc.origin =
        track == 0 ? AudioTrackOrigin::ANALOGUE : AudioTrackOrigin::EFM;
    desc.locked = (track == 0);
    desc.sample_rate = track == 0 ? locked_audio_sample_rate(VideoSystem::PAL)
                                  : kFreeRunningAudioRate;
    return desc;
  }

  uint32_t get_audio_sample_count(size_t track, FrameID id) const override {
    return (track == 0 && id == FrameID{0}) ? 1764u : 0u;
  }

  std::vector<int16_t> get_audio_samples(size_t track,
                                         FrameID id) const override {
    if (track != 0 || id != FrameID{0}) return {};
    return std::vector<int16_t>{10, -10, 20, -20};
  }

  uint64_t get_audio_stream_pair_count(size_t track) const override {
    return track == 1 ? 999u : 0u;
  }

  std::vector<int16_t> get_audio_stream_samples(
      size_t track, uint64_t first_pair, uint32_t pair_count) const override {
    if (track != 1) return {};
    return std::vector<int16_t>(static_cast<size_t>(pair_count) * 2,
                                static_cast<int16_t>(first_pair));
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
// Default accessor behaviour
// ---------------------------------------------------------------------------

TEST(AudioTrackContractTest, Defaults_ReportNoAudioOnEveryAccessor) {
  DefaultAudioSource source;

  EXPECT_EQ(source.audio_track_count(), 0u);
  EXPECT_FALSE(source.has_audio());
  EXPECT_FALSE(source.get_audio_track_descriptor(0).has_value());
  EXPECT_EQ(source.get_audio_sample_count(0, FrameID{0}), 0u);
  EXPECT_TRUE(source.get_audio_samples(0, FrameID{0}).empty());
  EXPECT_EQ(source.get_audio_stream_pair_count(0), 0u);
  EXPECT_TRUE(source.get_audio_stream_samples(0, 0, 4).empty());
}

TEST(AudioTrackContractTest, HasAudio_DerivesFromTrackCount) {
  TwoTrackSource source;
  EXPECT_TRUE(source.has_audio());
}

// ---------------------------------------------------------------------------
// Wrapper forwarding
// ---------------------------------------------------------------------------

TEST(AudioTrackContractTest, Wrapper_ForwardsAllSixTrackAccessors) {
  auto source = std::make_shared<TwoTrackSource>();
  PassThrough wrapper(source);

  EXPECT_EQ(wrapper.audio_track_count(), 2u);
  EXPECT_TRUE(wrapper.has_audio());

  const auto desc0 = wrapper.get_audio_track_descriptor(0);
  ASSERT_TRUE(desc0.has_value());
  EXPECT_EQ(desc0->name, "Analogue");
  EXPECT_EQ(desc0->origin, AudioTrackOrigin::ANALOGUE);
  EXPECT_TRUE(desc0->locked);

  const auto desc1 = wrapper.get_audio_track_descriptor(1);
  ASSERT_TRUE(desc1.has_value());
  EXPECT_EQ(desc1->origin, AudioTrackOrigin::EFM);
  EXPECT_FALSE(desc1->locked);
  EXPECT_EQ(desc1->sample_rate.num, 44100u);
  EXPECT_EQ(desc1->sample_rate.den, 1u);

  EXPECT_FALSE(wrapper.get_audio_track_descriptor(2).has_value());

  EXPECT_EQ(wrapper.get_audio_sample_count(0, FrameID{0}), 1764u);
  EXPECT_EQ(wrapper.get_audio_samples(0, FrameID{0}),
            (std::vector<int16_t>{10, -10, 20, -20}));

  EXPECT_EQ(wrapper.get_audio_stream_pair_count(1), 999u);
  const auto stream = wrapper.get_audio_stream_samples(1, 7, 3);
  ASSERT_EQ(stream.size(), 6u);
  EXPECT_EQ(stream[0], 7);
}

TEST(AudioTrackContractTest, Wrapper_ChainedForwardingReachesSource) {
  auto source = std::make_shared<TwoTrackSource>();
  auto first = std::make_shared<PassThrough>(source);
  PassThrough second(first);

  EXPECT_EQ(second.audio_track_count(), 2u);
  EXPECT_EQ(second.get_audio_sample_count(0, FrameID{0}), 1764u);
  EXPECT_EQ(second.get_audio_stream_pair_count(1), 999u);
}

TEST(AudioTrackContractTest, Wrapper_WithNullSource_ReturnsDefaults) {
  PassThrough wrapper(nullptr);

  EXPECT_EQ(wrapper.audio_track_count(), 0u);
  EXPECT_FALSE(wrapper.get_audio_track_descriptor(0).has_value());
  EXPECT_EQ(wrapper.get_audio_sample_count(0, FrameID{0}), 0u);
  EXPECT_TRUE(wrapper.get_audio_samples(0, FrameID{0}).empty());
  EXPECT_EQ(wrapper.get_audio_stream_pair_count(0), 0u);
  EXPECT_TRUE(wrapper.get_audio_stream_samples(0, 0, 4).empty());
}

// ---------------------------------------------------------------------------
// Locked sample rates
// ---------------------------------------------------------------------------

TEST(AudioTrackContractTest, LockedSampleRate_MatchesVideoSystem) {
  const auto pal = locked_audio_sample_rate(VideoSystem::PAL);
  EXPECT_EQ(pal.num, 44100u);
  EXPECT_EQ(pal.den, 1u);

  const auto ntsc = locked_audio_sample_rate(VideoSystem::NTSC);
  EXPECT_EQ(ntsc.num, 44100000u);
  EXPECT_EQ(ntsc.den, 1001u);

  const auto pal_m = locked_audio_sample_rate(VideoSystem::PAL_M);
  EXPECT_EQ(pal_m.num, 44100000u);
  EXPECT_EQ(pal_m.den, 1001u);

  EXPECT_EQ(locked_audio_sample_rate(VideoSystem::Unknown).num, 0u);
}

// ---------------------------------------------------------------------------
// audio_stream_pair_offset exactness
// ---------------------------------------------------------------------------

TEST(AudioTrackContractTest, PairOffset_PalLocked_ConstantWindows) {
  // PAL locked audio: 44100 Hz / 25 fps = exactly 1764 pairs per frame.
  const AudioSampleRate rate{44100, 1};
  for (uint64_t frame : {0ull, 1ull, 2ull, 100ull, 90000ull}) {
    EXPECT_EQ(audio_stream_pair_offset(frame, rate, VideoSystem::PAL),
              frame * 1764u)
        << "frame " << frame;
  }
}

TEST(AudioTrackContractTest, PairOffset_NtscLocked_ConstantWindows) {
  // NTSC locked audio: (44100000/1001) / (30000/1001) = exactly 1470 pairs.
  const AudioSampleRate rate{44100000, 1001};
  for (uint64_t frame : {0ull, 1ull, 2ull, 100ull, 107892ull}) {
    EXPECT_EQ(audio_stream_pair_offset(frame, rate, VideoSystem::NTSC),
              frame * 1470u)
        << "frame " << frame;
  }
}

TEST(AudioTrackContractTest, PairOffset_NtscFreeRunning_AlternatingWindows) {
  // A free-running 44100 Hz stream against 30000/1001 fps yields 1471.47
  // pairs per frame: window sizes alternate between 1471 and 1472 pairs and
  // must never produce any other size.
  const AudioSampleRate rate = kFreeRunningAudioRate;
  uint64_t prev = audio_stream_pair_offset(0, rate, VideoSystem::NTSC);
  EXPECT_EQ(prev, 0u);
  bool saw_1471 = false;
  bool saw_1472 = false;
  for (uint64_t frame = 1; frame <= 10000; ++frame) {
    const uint64_t offset =
        audio_stream_pair_offset(frame, rate, VideoSystem::NTSC);
    const uint64_t window = offset - prev;
    ASSERT_TRUE(window == 1471u || window == 1472u)
        << "frame " << frame << " window " << window;
    saw_1471 |= (window == 1471u);
    saw_1472 |= (window == 1472u);
    prev = offset;
  }
  EXPECT_TRUE(saw_1471);
  EXPECT_TRUE(saw_1472);
}

TEST(AudioTrackContractTest, PairOffset_NtscFreeRunning_NoCumulativeDrift) {
  // 44100 Hz × 1001/30000 = 1471.47 pairs/frame exactly; over N frames the
  // offset must equal round(N × 147147 / 100) — no accumulated error.
  const AudioSampleRate rate = kFreeRunningAudioRate;
  for (uint64_t frame : {30000ull, 1000000ull, 12345679ull}) {
    const uint64_t expected = (frame * 147147u + 50u) / 100u;
    EXPECT_EQ(audio_stream_pair_offset(frame, rate, VideoSystem::NTSC),
              expected)
        << "frame " << frame;
  }
}

TEST(AudioTrackContractTest, PairOffset_UnknownSystemOrZeroRate_ReturnsZero) {
  EXPECT_EQ(audio_stream_pair_offset(100, kFreeRunningAudioRate,
                                     VideoSystem::Unknown),
            0u);
  EXPECT_EQ(
      audio_stream_pair_offset(100, AudioSampleRate{0, 1}, VideoSystem::PAL),
      0u);
  EXPECT_EQ(audio_stream_pair_offset(100, AudioSampleRate{44100, 0},
                                     VideoSystem::PAL),
            0u);
}

}  // namespace orc_unit_test
