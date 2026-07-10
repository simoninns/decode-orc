/*
 * File:        multi_track_audio_roundtrip_test.cpp
 * Module:      orc-core-functional-tests
 * Purpose:     Multi-track audio round-trip through cvbs_sink and cvbs_source
 *              (real files: WAV sidecars and the .meta audio_track table)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <orc/stage/artifact.h>
#include <orc/stage/audio_track.h>
#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/observation_context.h>
#include <orc/stage/video_frame_representation.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cvbs_sink_stage_deps.h"
#include "cvbs_source_stage.h"

namespace orc_functional_test {

namespace {

constexpr size_t kFrameCount = 2;
constexpr uint32_t kPalLockedPairsPerFrame = 1764;
constexpr uint64_t kFreeRunningPairs = 5000;

// Deterministic sample generators so the read-back comparison is exact.
int16_t locked_sample(orc::FrameID frame_id, size_t index) {
  return static_cast<int16_t>((frame_id * 1000 + index) % 30000);
}

int16_t free_running_sample(uint64_t index) {
  return static_cast<int16_t>((index * 7) % 20000);
}

// Two PAL frames of synthetic video with two audio tracks: track 0
// frame-locked ("Analogue"), track 1 free-running ("EFM digital audio").
class MultiTrackVFR : public orc::VideoFrameRepresentation,
                      public orc::Artifact {
 public:
  MultiTrackVFR()
      : orc::Artifact(orc::ArtifactID("multi_track_vfr"), orc::Provenance{}) {
    for (size_t f = 0; f < kFrameCount; ++f) {
      std::vector<sample_type> frame(orc::kPalFrameSamples);
      for (size_t i = 0; i < frame.size(); ++i) {
        frame[i] = static_cast<sample_type>((f * 17 + i) % 1024);
      }
      frames_.push_back(std::move(frame));
    }
  }

  std::string type_name() const override { return "MultiTrackVFR"; }

  orc::FrameIDRange frame_range() const override {
    return {0, kFrameCount - 1};
  }
  size_t frame_count() const override { return kFrameCount; }
  bool has_frame(orc::FrameID id) const override { return id < kFrameCount; }

  std::optional<orc::FrameDescriptor> get_frame_descriptor(
      orc::FrameID id) const override {
    if (id >= kFrameCount) return std::nullopt;
    orc::FrameDescriptor desc;
    desc.frame_id = id;
    desc.system = orc::VideoSystem::PAL;
    desc.height = 625;
    desc.samples_total = orc::kPalFrameSamples;
    desc.samples_per_line_nominal = 1135;
    return desc;
  }

  const sample_type* get_frame(orc::FrameID id) const override {
    return (id < kFrameCount) ? frames_[id].data() : nullptr;
  }
  std::vector<sample_type> get_frame_copy(orc::FrameID id) const override {
    return (id < kFrameCount) ? frames_[id] : std::vector<sample_type>{};
  }

  // Audio tracks
  size_t audio_track_count() const override { return 2; }

  std::optional<orc::AudioTrackDescriptor> get_audio_track_descriptor(
      size_t track) const override {
    if (track == 0) {
      return orc::AudioTrackDescriptor{"Analogue",
                                       orc::AudioTrackOrigin::ANALOGUE, true,
                                       orc::AudioSampleRate{44100, 1}};
    }
    if (track == 1) {
      return orc::AudioTrackDescriptor{"EFM digital audio",
                                       orc::AudioTrackOrigin::EFM, false,
                                       orc::AudioSampleRate{44100, 1}};
    }
    return std::nullopt;
  }

  uint32_t get_audio_sample_count(size_t track,
                                  orc::FrameID id) const override {
    return (track == 0 && id < kFrameCount) ? kPalLockedPairsPerFrame : 0;
  }

  std::vector<int16_t> get_audio_samples(size_t track,
                                         orc::FrameID id) const override {
    if (track != 0 || id >= kFrameCount) return {};
    std::vector<int16_t> samples(static_cast<size_t>(kPalLockedPairsPerFrame) *
                                 2);
    for (size_t i = 0; i < samples.size(); ++i) {
      samples[i] = locked_sample(id, i);
    }
    return samples;
  }

  uint64_t get_audio_stream_pair_count(size_t track) const override {
    return (track == 1) ? kFreeRunningPairs : 0;
  }

  std::vector<int16_t> get_audio_stream_samples(
      size_t track, uint64_t first_pair, uint32_t pair_count) const override {
    if (track != 1 || first_pair >= kFreeRunningPairs) return {};
    const uint64_t available =
        std::min<uint64_t>(pair_count, kFreeRunningPairs - first_pair);
    std::vector<int16_t> samples(static_cast<size_t>(available) * 2);
    for (uint64_t i = 0; i < available * 2; ++i) {
      samples[i] = free_running_sample(first_pair * 2 + i);
    }
    return samples;
  }

 private:
  std::vector<std::vector<sample_type>> frames_;
};

}  // namespace

TEST(MultiTrackAudioRoundTrip, CVBSSinkToCVBSSource_PreservesTracks) {
  const std::filesystem::path dir =
      std::filesystem::path(::testing::TempDir()) / "orc_multi_track_roundtrip";
  std::filesystem::create_directories(dir);
  const std::string base = (dir / "roundtrip").string();

  // --- Write with the real cvbs_sink deps ---
  MultiTrackVFR vfr;
  std::atomic<bool> cancel{false};
  orc::CVBSSinkStageDeps sink_deps;
  sink_deps.init({}, &cancel);

  orc::CVBSSinkWriteConfig config;
  config.output_base_path = base;
  const auto write_result = sink_deps.write_cvbs(&vfr, config);
  ASSERT_TRUE(write_result.success) << write_result.status_message;
  EXPECT_EQ(write_result.frames_written, kFrameCount);

  EXPECT_TRUE(std::filesystem::exists(base + ".composite"));
  EXPECT_TRUE(std::filesystem::exists(base + ".meta"));
  EXPECT_TRUE(std::filesystem::exists(base + "_audio_00.wav"));
  EXPECT_TRUE(std::filesystem::exists(base + "_audio_01.wav"));

  // Locked track: 44-byte header + 2 frames × 1764 pairs × 4 bytes.
  EXPECT_EQ(std::filesystem::file_size(base + "_audio_00.wav"),
            44u + kFrameCount * kPalLockedPairsPerFrame * 4u);
  // Free-running track: 44-byte header + 5000 pairs × 4 bytes.
  EXPECT_EQ(std::filesystem::file_size(base + "_audio_01.wav"),
            44u + kFreeRunningPairs * 4u);

  // --- Read back with the real cvbs_source stage ---
  orc::PALCVBSSourceStage source;
  const std::map<std::string, orc::ParameterValue> parameters{
      {"input_path", base + ".composite"}};
  ASSERT_TRUE(source.set_parameters(parameters));

  orc::ObservationContext observation_context;
  const auto outputs = source.execute({}, parameters, observation_context);
  ASSERT_EQ(outputs.size(), 1u);

  const auto read_vfr =
      std::dynamic_pointer_cast<orc::VideoFrameRepresentation>(outputs[0]);
  ASSERT_NE(read_vfr, nullptr);
  EXPECT_EQ(read_vfr->frame_count(), kFrameCount);

  // --- Descriptors round-trip via the .meta audio_track table ---
  ASSERT_EQ(read_vfr->audio_track_count(), 2u);

  const auto track0 = read_vfr->get_audio_track_descriptor(0);
  ASSERT_TRUE(track0.has_value());
  EXPECT_EQ(track0->name, "Analogue");
  EXPECT_TRUE(track0->locked);
  // The container carries no origin metadata.
  EXPECT_EQ(track0->origin, orc::AudioTrackOrigin::UNKNOWN);

  const auto track1 = read_vfr->get_audio_track_descriptor(1);
  ASSERT_TRUE(track1.has_value());
  EXPECT_EQ(track1->name, "EFM digital audio");
  EXPECT_FALSE(track1->locked);
  EXPECT_EQ(track1->sample_rate.num, 44100u);
  EXPECT_EQ(track1->sample_rate.den, 1u);

  // --- Locked samples round-trip per frame ---
  for (orc::FrameID fid = 0; fid < kFrameCount; ++fid) {
    EXPECT_EQ(read_vfr->get_audio_sample_count(0, fid),
              kPalLockedPairsPerFrame);
    const auto samples = read_vfr->get_audio_samples(0, fid);
    ASSERT_EQ(samples.size(), static_cast<size_t>(kPalLockedPairsPerFrame) * 2);
    bool all_equal = true;
    for (size_t i = 0; i < samples.size(); ++i) {
      if (samples[i] != locked_sample(fid, i)) {
        all_equal = false;
        break;
      }
    }
    EXPECT_TRUE(all_equal) << "locked samples differ in frame " << fid;
  }

  // --- Free-running stream round-trips via the stream accessors ---
  EXPECT_EQ(read_vfr->get_audio_stream_pair_count(1), kFreeRunningPairs);

  const auto full_stream = read_vfr->get_audio_stream_samples(
      1, 0, static_cast<uint32_t>(kFreeRunningPairs));
  ASSERT_EQ(full_stream.size(), static_cast<size_t>(kFreeRunningPairs) * 2);
  bool stream_equal = true;
  for (size_t i = 0; i < full_stream.size(); ++i) {
    if (full_stream[i] != free_running_sample(i)) {
      stream_equal = false;
      break;
    }
  }
  EXPECT_TRUE(stream_equal) << "free-running stream differs";

  // A mid-stream slice reads at the correct pair offset.
  const auto slice = read_vfr->get_audio_stream_samples(1, 1000, 4);
  ASSERT_EQ(slice.size(), 8u);
  for (size_t i = 0; i < slice.size(); ++i) {
    EXPECT_EQ(slice[i], free_running_sample(2000 + i));
  }

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

}  // namespace orc_functional_test
