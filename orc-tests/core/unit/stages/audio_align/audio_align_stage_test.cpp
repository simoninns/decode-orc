/*
 * File:        audio_align_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for AudioAlignStage (cadence-aware window assembly
 *              with edge silence, 48 pairs/ms offset conversion)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/audio_align/audio_align_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <orc/stage/audio_channel_pair.h>
#include <orc/stage/observation_context.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../../include/video_frame_representation_artifact_mock.h"

namespace orc_unit_test {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descs,
    const std::string& name) {
  auto it = std::find_if(
      descs.begin(), descs.end(),
      [&](const orc::ParameterDescriptor& d) { return d.name == name; });
  return it == descs.end() ? nullptr : &(*it);
}

// Hand-built stream: the stereo pair at absolute stream position |pos|
// (indexed by audio_pair_offset) carries pos + 1 on the left channel and
// -(pos + 1) on the right, so position 0 never collides with silence.
int32_t left_value(int64_t pos) { return static_cast<int32_t>(pos) + 1; }
int32_t right_value(int64_t pos) { return -(static_cast<int32_t>(pos) + 1); }

// Synchronous single-pair source of |frame_count| frames for |system|; frame
// f serves the hand-built stream positions [audio_pair_offset(f),
// audio_pair_offset(f + 1)) per the cadence.
std::shared_ptr<NiceMock<MockVideoFrameRepresentationArtifact>>
make_synchronous_source(orc::VideoSystem system, uint64_t frame_count) {
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(1u));
  ON_CALL(*vfr, get_audio_channel_pair_descriptor(0))
      .WillByDefault(Return(orc::AudioChannelPairDescriptor{
          "Analogue", orc::AudioOrigin::ANALOGUE}));
  ON_CALL(*vfr, frame_range())
      .WillByDefault(Return(
          orc::FrameIDRange{orc::FrameID(0), orc::FrameID(frame_count - 1)}));
  orc::SourceParameters params;
  params.system = system;
  ON_CALL(*vfr, get_video_parameters()).WillByDefault(Return(params));
  for (uint64_t f = 0; f < frame_count; ++f) {
    std::vector<int32_t> samples;
    const uint64_t begin = orc::audio_pair_offset(f, system);
    const uint64_t end = orc::audio_pair_offset(f + 1, system);
    for (uint64_t pos = begin; pos < end; ++pos) {
      samples.push_back(left_value(static_cast<int64_t>(pos)));
      samples.push_back(right_value(static_cast<int64_t>(pos)));
    }
    ON_CALL(*vfr, get_audio_samples(0, orc::FrameID(f)))
        .WillByDefault(Return(samples));
  }
  return vfr;
}

// Expected shifted window for output frame |id|: output pair k reads the
// hand-built stream at position audio_pair_offset(id) + k - offset_pairs,
// silence outside [0, audio_pair_offset(frame_count)).
std::vector<int32_t> expected_window(orc::VideoSystem system, uint64_t id,
                                     int64_t offset_pairs,
                                     uint64_t frame_count) {
  const uint64_t pairs = orc::audio_pairs_in_frame(id, system);
  const int64_t stream_end =
      static_cast<int64_t>(orc::audio_pair_offset(frame_count, system));
  std::vector<int32_t> out;
  out.reserve(pairs * 2);
  for (uint64_t k = 0; k < pairs; ++k) {
    const int64_t pos =
        static_cast<int64_t>(orc::audio_pair_offset(id, system)) +
        static_cast<int64_t>(k) - offset_pairs;
    if (pos < 0 || pos >= stream_end) {
      out.push_back(0);
      out.push_back(0);
    } else {
      out.push_back(left_value(pos));
      out.push_back(right_value(pos));
    }
  }
  return out;
}

TEST(AudioAlignStageTest, NodeTypeInfo_ReportsTransformWithOneInput) {
  orc::AudioAlignStage stage;
  const auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "audio_align");
  EXPECT_EQ(stage.required_input_count(), 1u);
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(AudioAlignStageTest, Descriptors_DefaultsRoundTripThroughSetGet) {
  orc::AudioAlignStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  const auto* channel_pair = find_descriptor(descriptors, "channel_pair");
  const auto* offset = find_descriptor(descriptors, "offset_ms");
  ASSERT_NE(channel_pair, nullptr);
  ASSERT_NE(offset, nullptr);
  EXPECT_EQ(channel_pair->type, orc::ParameterType::STRING);
  EXPECT_EQ(offset->type, orc::ParameterType::DOUBLE);
  EXPECT_EQ(std::get<std::string>(*channel_pair->constraints.default_value),
            "0");
  // One allowed string per container channel-pair slot; the GUI narrows this
  // to the pairs the input actually carries.
  EXPECT_EQ(channel_pair->constraints.allowed_strings.size(),
            orc::kMaxAudioChannelPairs);
  EXPECT_EQ(std::get<double>(*offset->constraints.default_value), 0.0);

  EXPECT_TRUE(stage.set_parameters(
      {{"channel_pair", std::string("2")}, {"offset_ms", -12.5}}));
  const auto params = stage.get_parameters();
  EXPECT_EQ(std::get<std::string>(params.at("channel_pair")), "2");
  EXPECT_EQ(std::get<double>(params.at("offset_ms")), -12.5);
}

TEST(AudioAlignStageTest, Execute_ThrowsWhenChannelPairOutOfRange) {
  orc::AudioAlignStage stage;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(1u));

  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({vfr}, {{"channel_pair", std::string("1")}}, ctx),
               orc::DAGExecutionError);
}

TEST(AudioAlignStageTest, Execute_ZeroOffsetPassesInputThrough) {
  orc::AudioAlignStage stage;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(1u));

  orc::ObservationContext ctx;
  auto outputs = stage.execute({vfr}, {{"offset_ms", 0.0}}, ctx);
  ASSERT_EQ(outputs.size(), 1u);
  EXPECT_EQ(outputs[0].get(), vfr.get());
}

TEST(AudioAlignStageTest, Execute_ConvertsMillisecondsAt48PairsPerMs) {
  // 1 ms at the synchronous 48000 Hz rate is exactly 48 stereo pairs: PAL
  // frame 0 gains a 48-pair lead-in of silence, then the stream from
  // position 0.
  orc::AudioAlignStage stage;
  auto vfr = make_synchronous_source(orc::VideoSystem::PAL, 2);

  orc::ObservationContext ctx;
  auto outputs = stage.execute({vfr}, {{"offset_ms", 1.0}}, ctx);
  ASSERT_EQ(outputs.size(), 1u);
  auto output = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
      outputs[0]);
  ASSERT_NE(output, nullptr);

  const auto window = output->get_audio_samples(0, orc::FrameID(0));
  ASSERT_EQ(window.size(), 1920u * 2);
  EXPECT_EQ(window[47 * 2], 0);              // last lead-in pair
  EXPECT_EQ(window[48 * 2], left_value(0));  // stream position 0
  EXPECT_EQ(window[48 * 2 + 1], right_value(0));
  EXPECT_EQ(window, expected_window(orc::VideoSystem::PAL, 0, 48, 2));
}

TEST(AudioAlignStageTest,
     NtscPositiveOffset_AssemblesCadenceWindowsAcrossFrameBoundaries) {
  // +5 pairs on NTSC: every output window straddles a frame boundary, and
  // neighbouring frames carry 1602 vs 1601 pairs (1602 when id mod 5 is in
  // {0, 2, 4}). Verify each window against the hand-built stream indexed by
  // audio_pair_offset.
  auto vfr = make_synchronous_source(orc::VideoSystem::NTSC, 5);
  const orc::AlignedAudioChannelPairRepresentation aligned(vfr, 0, 5);

  for (uint64_t f = 0; f < 5; ++f) {
    const auto window = aligned.get_audio_samples(0, orc::FrameID(f));
    // Per-frame pair counts are unchanged by the shift (cadence preserved).
    ASSERT_EQ(window.size(),
              orc::audio_pairs_in_frame(f, orc::VideoSystem::NTSC) * 2u)
        << "frame " << f;
    EXPECT_EQ(window, expected_window(orc::VideoSystem::NTSC, f, 5, 5))
        << "frame " << f;
  }

  // Spot checks. Frame 0 (1602 pairs): five silence pairs then position 0.
  const auto frame0 = aligned.get_audio_samples(0, orc::FrameID(0));
  ASSERT_EQ(frame0.size(), 1602u * 2);
  EXPECT_EQ(frame0[4 * 2], 0);
  EXPECT_EQ(frame0[5 * 2], left_value(0));

  // Frame 1 (1601 pairs) starts at stream position 1597 — the last five
  // pairs of source frame 0 — and crosses into frame 1 at position 1602.
  const auto frame1 = aligned.get_audio_samples(0, orc::FrameID(1));
  ASSERT_EQ(frame1.size(), 1601u * 2);
  EXPECT_EQ(frame1[0], left_value(1597));
  EXPECT_EQ(frame1[1], right_value(1597));
  EXPECT_EQ(frame1[4 * 2], left_value(1601));  // last pair of source frame 0
  EXPECT_EQ(frame1[5 * 2], left_value(1602));  // first pair of source frame 1
  EXPECT_EQ(frame1[1600 * 2], left_value(3197));
}

TEST(AudioAlignStageTest, NtscNegativeOffset_SilencesPastStreamEnd) {
  // -5 pairs on NTSC: the last frame's window reads past the end of the
  // 8008-pair 5-frame sequence and gets a five-pair silence tail.
  auto vfr = make_synchronous_source(orc::VideoSystem::NTSC, 5);
  const orc::AlignedAudioChannelPairRepresentation aligned(vfr, 0, -5);

  for (uint64_t f = 0; f < 5; ++f) {
    EXPECT_EQ(aligned.get_audio_samples(0, orc::FrameID(f)),
              expected_window(orc::VideoSystem::NTSC, f, -5, 5))
        << "frame " << f;
  }

  // Frame 4 (1602 pairs, offset 6406): positions [6411, 8013) — the stream
  // ends at 8008, so the last five pairs are silence.
  const auto frame4 = aligned.get_audio_samples(0, orc::FrameID(4));
  ASSERT_EQ(frame4.size(), 1602u * 2);
  EXPECT_EQ(frame4[0], left_value(6411));
  EXPECT_EQ(frame4[1596 * 2], left_value(8007));  // last real stream pair
  EXPECT_EQ(frame4[1597 * 2], 0);
  EXPECT_EQ(frame4.back(), 0);
}

TEST(AudioAlignStageTest, LargeOffset_SilencesEntirelyOutOfRangeWindows) {
  // A delay longer than one frame leaves frame 0 fully silent on PAL and
  // shifts frame 1 back to the start of the stream.
  auto vfr = make_synchronous_source(orc::VideoSystem::PAL, 2);
  const orc::AlignedAudioChannelPairRepresentation aligned(vfr, 0, 1920);

  const auto frame0 = aligned.get_audio_samples(0, orc::FrameID(0));
  ASSERT_EQ(frame0.size(), 1920u * 2);
  EXPECT_TRUE(std::all_of(frame0.begin(), frame0.end(),
                          [](int32_t s) { return s == 0; }));
  EXPECT_EQ(aligned.get_audio_samples(0, orc::FrameID(1)),
            expected_window(orc::VideoSystem::PAL, 1, 1920, 2));
}

TEST(AudioAlignStageTest, NonTargetPairs_ForwardUntouched) {
  auto vfr = make_synchronous_source(orc::VideoSystem::PAL, 2);
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(2u));
  ON_CALL(*vfr, get_audio_samples(1, orc::FrameID(1)))
      .WillByDefault(Return(std::vector<int32_t>{42, -42}));

  const orc::AlignedAudioChannelPairRepresentation aligned(vfr, 0, 100);

  EXPECT_EQ(aligned.get_audio_samples(1, orc::FrameID(1)),
            (std::vector<int32_t>{42, -42}));
}

TEST(AudioAlignStageTest, OutOfRangePair_ReturnsEmpty) {
  auto vfr = make_synchronous_source(orc::VideoSystem::PAL, 2);
  const orc::AlignedAudioChannelPairRepresentation aligned(vfr, 0, 100);

  EXPECT_TRUE(aligned.get_audio_samples(1, orc::FrameID(0)).empty());
  EXPECT_TRUE(aligned.get_audio_samples(5, orc::FrameID(0)).empty());
}

TEST(AudioAlignStageTest, MissingVideoParameters_YieldsEmptyWindow) {
  // Without video parameters the system (and thus the cadence) is unknown;
  // the shifted target pair has no defined audio layout.
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(1u));

  const orc::AlignedAudioChannelPairRepresentation aligned(vfr, 0, 10);

  EXPECT_TRUE(aligned.get_audio_samples(0, orc::FrameID(0)).empty());
}

}  // namespace
}  // namespace orc_unit_test
