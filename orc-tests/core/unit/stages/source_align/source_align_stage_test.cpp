/*
 * File:        source_align_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for SourceAlignStage (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/source_align/source_align_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <orc/stage/observation_context.h>

#include <algorithm>
#include <cstdint>

#include "../../include/video_frame_representation_artifact_mock.h"

using ::testing::NiceMock;
using ::testing::Return;

namespace orc_unit_test {
namespace {

const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::string& name) {
  auto it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [&](const orc::ParameterDescriptor& d) { return d.name == name; });
  return it == descriptors.end() ? nullptr : &(*it);
}

}  // namespace

TEST(SourceAlignStageTest, RequiredInputCount_IsOne) {
  orc::SourceAlignStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(SourceAlignStageTest, OutputCount_IsUnboundedSentinel) {
  orc::SourceAlignStage stage;
  EXPECT_EQ(stage.output_count(), static_cast<size_t>(UINT32_MAX));
}

TEST(SourceAlignStageTest, NodeTypeInfo_HasExpectedMetadata) {
  orc::SourceAlignStage stage;
  auto info = stage.get_node_type_info();
  EXPECT_EQ(info.type, orc::NodeType::COMPLEX);
  EXPECT_EQ(info.stage_name, "source_align");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(SourceAlignStageTest, Descriptor_DefaultsMatchRuntimeDefaults) {
  orc::SourceAlignStage stage;
  const auto descriptors = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* alignment_map = find_descriptor(descriptors, "alignmentMap");
  ASSERT_NE(alignment_map, nullptr);
  ASSERT_TRUE(alignment_map->constraints.default_value.has_value());
  EXPECT_EQ(std::get<std::string>(*alignment_map->constraints.default_value),
            std::get<std::string>(params.at("alignmentMap")));
}

TEST(SourceAlignStageTest, SetParameters_AcceptsAlignmentMapString) {
  orc::SourceAlignStage stage;
  EXPECT_TRUE(stage.set_parameters({{"alignmentMap", std::string("1+2,2+4")}}));
  const auto params = stage.get_parameters();
  EXPECT_EQ(std::get<std::string>(params.at("alignmentMap")), "1+2,2+4");
}

TEST(SourceAlignStageTest, SetParameters_RejectsUnknownParameter) {
  orc::SourceAlignStage stage;
  EXPECT_FALSE(stage.set_parameters({{"unknown", true}}));
}

TEST(SourceAlignStageTest, Execute_ThrowsWhenNoInputsProvided) {
  orc::SourceAlignStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({}, {}, ctx), orc::DAGExecutionError);
}

TEST(SourceAlignStageTest, Execute_ThrowsWhenTooManyInputsProvided) {
  orc::SourceAlignStage stage;
  orc::ObservationContext ctx;
  std::vector<orc::ArtifactPtr> inputs(17);
  EXPECT_THROW(stage.execute(inputs, {}, ctx), orc::DAGExecutionError);
}

TEST(SourceAlignStageTest, Execute_ThrowsWhenInputIsWrongType) {
  struct FakeArt : public orc::Artifact {
    FakeArt() : Artifact(orc::ArtifactID("x"), orc::Provenance{}) {}
    std::string type_name() const override { return "x"; }
  };
  orc::SourceAlignStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({std::make_shared<FakeArt>()}, {}, ctx),
               orc::DAGExecutionError);
}

TEST(SourceAlignStageTest, Execute_ThrowsWhenManualAlignmentMapIsInvalid) {
  orc::SourceAlignStage stage;
  orc::ObservationContext ctx;
  auto source =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  ON_CALL(*source, type_name()).WillByDefault(Return("test_vfr_artifact"));

  EXPECT_THROW(
      stage.execute({source}, {{"alignmentMap", std::string("invalid")}}, ctx),
      orc::DAGExecutionError);
}

TEST(SourceAlignStageTest, Execute_PassesThroughSingleSourceWithZeroOffset) {
  orc::SourceAlignStage stage;
  orc::ObservationContext ctx;
  auto source =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  ON_CALL(*source, type_name()).WillByDefault(Return("test_vfr_artifact"));
  ON_CALL(*source, frame_range())
      .WillByDefault(Return(orc::FrameIDRange{0u, 9u}));
  ON_CALL(*source, frame_count()).WillByDefault(Return(10u));

  const auto outputs = stage.execute({source}, {}, ctx);

  ASSERT_EQ(outputs.size(), 1u);
  EXPECT_EQ(outputs[0].get(), source.get());  // pass-through pointer equality
}

TEST(SourceAlignStageTest, Execute_AppliesManualOffset_InFirstCommonFrameMode) {
  orc::SourceAlignStage stage;
  orc::ObservationContext ctx;
  auto source =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  ON_CALL(*source, type_name()).WillByDefault(Return("test_vfr_artifact"));
  ON_CALL(*source, frame_range())
      .WillByDefault(Return(orc::FrameIDRange{0u, 9u}));  // 10 frames
  ON_CALL(*source, frame_count()).WillByDefault(Return(10u));

  ASSERT_TRUE(stage.set_parameters(
      {{"alignmentMode", std::string("first_common_frame")},
       {"alignmentMap", std::string("1+2")}}));

  const auto outputs = stage.execute({source}, {}, ctx);

  ASSERT_EQ(outputs.size(), 1u);
  EXPECT_NE(outputs[0].get(), source.get());

  // Skip offset 2: output should have 10 - 2 = 8 frames.
  auto vfr =
      std::dynamic_pointer_cast<orc::VideoFrameRepresentation>(outputs[0]);
  ASSERT_NE(vfr, nullptr);
  EXPECT_EQ(vfr->frame_count(), 8u);
}

TEST(SourceAlignStageTest,
     Execute_PrependsPaddingFrames_InPadForAlignmentMode) {
  orc::SourceAlignStage stage;
  orc::ObservationContext ctx;
  auto source =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  ON_CALL(*source, type_name()).WillByDefault(Return("test_vfr_artifact"));
  ON_CALL(*source, frame_range())
      .WillByDefault(Return(orc::FrameIDRange{0u, 9u}));  // 10 frames
  ON_CALL(*source, frame_count()).WillByDefault(Return(10u));

  ASSERT_TRUE(
      stage.set_parameters({{"alignmentMode", std::string("pad_for_alignment")},
                            {"alignmentMap", std::string("1+3")}}));

  const auto outputs = stage.execute({source}, {}, ctx);

  ASSERT_EQ(outputs.size(), 1u);
  EXPECT_NE(outputs[0].get(), source.get());

  // Pad prefix 3: output should have 3 + 10 = 13 frames.
  auto vfr =
      std::dynamic_pointer_cast<orc::VideoFrameRepresentation>(outputs[0]);
  ASSERT_NE(vfr, nullptr);
  EXPECT_EQ(vfr->frame_count(), 13u);

  // First 3 frames must be marked as padding.
  for (orc::FrameID fid = 0; fid < 3; ++fid) {
    EXPECT_TRUE(vfr->has_frame(fid));
    auto desc = vfr->get_frame_descriptor(fid);
    ASSERT_TRUE(desc.has_value()) << "no descriptor for padding frame " << fid;
    EXPECT_TRUE(desc->is_padding_frame) << "frame " << fid << " not padding";
  }
}

// ── Audio channel pairs through the alignment decorators ────────────────────

namespace {

// Interleaved stereo samples for source frame |frame|: stereo pair p carries
// the value frame × 10000 + p on both channels, sized to the frame's native
// SMPTE 272M-1994 §14.3 cadence count — so any output sample is traceable to
// its source frame and in-frame pair position.
std::vector<int32_t> frame_audio(uint64_t frame, orc::VideoSystem system) {
  const uint32_t pairs = orc::audio_pairs_in_frame(frame, system);
  std::vector<int32_t> samples;
  samples.reserve(static_cast<size_t>(pairs) * 2);
  for (uint32_t p = 0; p < pairs; ++p) {
    const auto value = static_cast<int32_t>(frame * 10000 + p);
    samples.push_back(value);
    samples.push_back(value);
  }
  return samples;
}

// 10-frame mocked source carrying one audio channel pair whose per-frame
// samples come from frame_audio(); no filesystem or real media involved.
std::shared_ptr<NiceMock<MockVideoFrameRepresentationArtifact>>
make_audio_source(orc::VideoSystem system) {
  auto source =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*source, type_name()).WillByDefault(Return("test_vfr_artifact"));
  ON_CALL(*source, frame_range())
      .WillByDefault(Return(orc::FrameIDRange{0u, 9u}));
  ON_CALL(*source, frame_count()).WillByDefault(Return(10u));
  orc::SourceParameters params;
  params.system = system;
  params.frame_width_nominal = 4;
  params.frame_height = 4;
  params.blanking_level = 256;
  ON_CALL(*source, get_video_parameters()).WillByDefault(Return(params));

  ON_CALL(*source, audio_channel_pair_count()).WillByDefault(Return(1u));
  ON_CALL(*source, get_audio_channel_pair_descriptor(0))
      .WillByDefault(Return(orc::AudioChannelPairDescriptor{
          "Analogue", orc::AudioOrigin::ANALOGUE}));
  ON_CALL(*source, get_audio_samples(0, ::testing::_))
      .WillByDefault(::testing::Invoke([system](size_t, orc::FrameID id) {
        return frame_audio(id, system);
      }));
  return source;
}

// Runs the stage over one source and returns the wrapped output.
std::shared_ptr<orc::VideoFrameRepresentation> align_one(
    orc::SourceAlignStage& stage, orc::ArtifactPtr source,
    const std::string& mode, const std::string& map) {
  orc::ObservationContext ctx;
  EXPECT_TRUE(
      stage.set_parameters({{"alignmentMode", mode}, {"alignmentMap", map}}));
  auto outputs = stage.execute({std::move(source)}, {}, ctx);
  EXPECT_EQ(outputs.size(), 1u);
  return std::dynamic_pointer_cast<orc::VideoFrameRepresentation>(outputs[0]);
}

}  // namespace

TEST(SourceAlignStageTest, TrimMode_PairCountAndDescriptorForwardFromSource) {
  orc::SourceAlignStage stage;
  auto source = make_audio_source(orc::VideoSystem::PAL);

  auto aligned = align_one(stage, source, "first_common_frame", "1+2");
  ASSERT_NE(aligned, nullptr);

  EXPECT_EQ(aligned->audio_channel_pair_count(), 1u);
  const auto desc = aligned->get_audio_channel_pair_descriptor(0);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->name, "Analogue");
  EXPECT_EQ(desc->origin, orc::AudioOrigin::ANALOGUE);
}

TEST(SourceAlignStageTest, TrimMode_PalAudioFollowsShiftedFramesSampleExact) {
  orc::SourceAlignStage stage;
  auto source = make_audio_source(orc::VideoSystem::PAL);

  // PAL is constant-cadence (1920 pairs per frame), so any trim offset is
  // sample-exact: output frame 0 serves source frame 2's audio untouched.
  auto aligned = align_one(stage, source, "first_common_frame", "1+2");
  ASSERT_NE(aligned, nullptr);

  const auto samples = aligned->get_audio_samples(0, orc::FrameID{0});
  EXPECT_EQ(samples, frame_audio(2, orc::VideoSystem::PAL));
  EXPECT_EQ(samples.size(), 1920u * 2u);
}

TEST(SourceAlignStageTest, TrimMode_PhasePreservingNtscOffsetIsSampleExact) {
  orc::SourceAlignStage stage;
  auto source = make_audio_source(orc::VideoSystem::NTSC);

  // Offset 5 preserves the SMPTE 272M five-frame audio sequence phase, so
  // every shifted frame is served sample-exact.
  auto aligned = align_one(stage, source, "first_common_frame", "1+5");
  ASSERT_NE(aligned, nullptr);

  for (uint64_t p = 0; p < 5; ++p) {
    EXPECT_EQ(aligned->get_audio_samples(0, orc::FrameID{p}),
              frame_audio(p + 5, orc::VideoSystem::NTSC))
        << "output frame " << p;
  }
}

TEST(SourceAlignStageTest, TrimMode_PhaseBreakingNtscOffsetObeysOnePairRule) {
  orc::SourceAlignStage stage;
  auto source = make_audio_source(orc::VideoSystem::NTSC);

  // Offset 1 breaks the SMPTE 272M-1994 §14.3 five-frame sequence phase.
  auto aligned = align_one(stage, source, "first_common_frame", "1+1");
  ASSERT_NE(aligned, nullptr);

  // Output frame 0 needs 1602 pairs; source frame 1 natively carries 1601 —
  // one trailing silence pair is appended.
  const auto padded = aligned->get_audio_samples(0, orc::FrameID{0});
  ASSERT_EQ(padded.size(), 1602u * 2u);
  const auto native1 = frame_audio(1, orc::VideoSystem::NTSC);
  EXPECT_TRUE(std::equal(native1.begin(), native1.end(), padded.begin()));
  EXPECT_EQ(padded[padded.size() - 2], 0);
  EXPECT_EQ(padded[padded.size() - 1], 0);

  // Output frame 1 needs 1601 pairs; source frame 2 natively carries 1602 —
  // the trailing pair is truncated.
  const auto truncated = aligned->get_audio_samples(0, orc::FrameID{1});
  ASSERT_EQ(truncated.size(), 1601u * 2u);
  auto expected = frame_audio(2, orc::VideoSystem::NTSC);
  expected.resize(1601u * 2u);
  EXPECT_EQ(truncated, expected);
}

TEST(SourceAlignStageTest, PadMode_PaddingFramesCarryCadenceSizedSilence) {
  orc::SourceAlignStage stage;
  auto source = make_audio_source(orc::VideoSystem::NTSC);

  auto padded = align_one(stage, source, "pad_for_alignment", "1+2");
  ASSERT_NE(padded, nullptr);

  // Silence blocks are sized by the OUTPUT frame's cadence position
  // (SMPTE 272M-1994 §14.3): 1602 pairs at index 0, 1601 at index 1.
  const auto silence0 = padded->get_audio_samples(0, orc::FrameID{0});
  EXPECT_EQ(silence0.size(), 1602u * 2u);
  EXPECT_TRUE(std::all_of(silence0.begin(), silence0.end(),
                          [](int32_t s) { return s == 0; }));
  const auto silence1 = padded->get_audio_samples(0, orc::FrameID{1});
  EXPECT_EQ(silence1.size(), 1601u * 2u);
  EXPECT_TRUE(std::all_of(silence1.begin(), silence1.end(),
                          [](int32_t s) { return s == 0; }));

  // Out-of-range pairs stay empty, including on padding frames.
  EXPECT_TRUE(padded->get_audio_samples(1, orc::FrameID{0}).empty());
}

TEST(SourceAlignStageTest, PadMode_ShiftedRealFramesObeyOnePairRule) {
  orc::SourceAlignStage stage;
  auto source = make_audio_source(orc::VideoSystem::NTSC);

  // Pad count 1 breaks the SMPTE 272M-1994 §14.3 five-frame sequence phase.
  auto padded = align_one(stage, source, "pad_for_alignment", "1+1");
  ASSERT_NE(padded, nullptr);

  // Output frame 1 needs 1601 pairs; source frame 0 natively carries 1602 —
  // the trailing pair is truncated.
  const auto truncated = padded->get_audio_samples(0, orc::FrameID{1});
  ASSERT_EQ(truncated.size(), 1601u * 2u);
  auto expected = frame_audio(0, orc::VideoSystem::NTSC);
  expected.resize(1601u * 2u);
  EXPECT_EQ(truncated, expected);

  // Output frame 2 needs 1602 pairs; source frame 1 natively carries 1601 —
  // one trailing silence pair is appended.
  const auto appended = padded->get_audio_samples(0, orc::FrameID{2});
  ASSERT_EQ(appended.size(), 1602u * 2u);
  const auto native1 = frame_audio(1, orc::VideoSystem::NTSC);
  EXPECT_TRUE(std::equal(native1.begin(), native1.end(), appended.begin()));
  EXPECT_EQ(appended[appended.size() - 2], 0);
  EXPECT_EQ(appended[appended.size() - 1], 0);
}

TEST(SourceAlignStageTest, PadMode_PalAudioFollowsShiftedFramesSampleExact) {
  orc::SourceAlignStage stage;
  auto source = make_audio_source(orc::VideoSystem::PAL);

  auto padded = align_one(stage, source, "pad_for_alignment", "1+2");
  ASSERT_NE(padded, nullptr);

  // PAL is constant-cadence, so shifted real frames are sample-exact and
  // padding silence is the constant 1920 pairs.
  const auto silence = padded->get_audio_samples(0, orc::FrameID{1});
  ASSERT_EQ(silence.size(), 1920u * 2u);
  EXPECT_TRUE(std::all_of(silence.begin(), silence.end(),
                          [](int32_t s) { return s == 0; }));
  EXPECT_EQ(padded->get_audio_samples(0, orc::FrameID{2}),
            frame_audio(0, orc::VideoSystem::PAL));
}

}  // namespace orc_unit_test
