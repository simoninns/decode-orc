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

// ── Audio track handling through the alignment decorators ───────────────────

namespace {

// PAL source with 10 frames and two audio tracks: locked track 0 and
// free-running track 1 (44100 Hz, 5000 pairs). PAL keeps the stream shift a
// constant 1764 pairs per frame, so a 2-frame offset is exactly 3528 pairs.
std::shared_ptr<NiceMock<MockVideoFrameRepresentationArtifact>>
make_two_track_pal_source() {
  auto source =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*source, type_name()).WillByDefault(Return("test_vfr_artifact"));
  ON_CALL(*source, frame_range())
      .WillByDefault(Return(orc::FrameIDRange{0u, 9u}));
  ON_CALL(*source, frame_count()).WillByDefault(Return(10u));
  orc::SourceParameters params;
  params.system = orc::VideoSystem::PAL;
  params.frame_width_nominal = 4;
  params.frame_height = 4;
  params.blanking_level = 256;
  ON_CALL(*source, get_video_parameters()).WillByDefault(Return(params));

  ON_CALL(*source, audio_track_count()).WillByDefault(Return(2u));
  ON_CALL(*source, get_audio_track_descriptor(0))
      .WillByDefault(Return(orc::AudioTrackDescriptor{
          "Analogue", orc::AudioTrackOrigin::ANALOGUE, true,
          orc::locked_audio_sample_rate(orc::VideoSystem::PAL)}));
  ON_CALL(*source, get_audio_track_descriptor(1))
      .WillByDefault(Return(orc::AudioTrackDescriptor{
          "EFM digital audio", orc::AudioTrackOrigin::EFM, false,
          orc::kFreeRunningAudioRate}));
  ON_CALL(*source, get_audio_stream_pair_count(1)).WillByDefault(Return(5000u));
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

TEST(SourceAlignStageTest, TrimMode_ShiftsFreeRunningStreamInTimeDomain) {
  orc::SourceAlignStage stage;
  auto source = make_two_track_pal_source();
  // Trimming 2 leading frames trims 2 × 1764 = 3528 leading stream pairs.
  ON_CALL(*source, get_audio_stream_samples(1, 3528, 4))
      .WillByDefault(Return(std::vector<int16_t>{1, 1, 2, 2, 3, 3, 4, 4}));

  auto aligned = align_one(stage, source, "first_common_frame", "1+2");
  ASSERT_NE(aligned, nullptr);

  EXPECT_EQ(aligned->get_audio_stream_pair_count(1), 5000u - 3528u);
  EXPECT_EQ(aligned->get_audio_stream_samples(1, 0, 4),
            (std::vector<int16_t>{1, 1, 2, 2, 3, 3, 4, 4}));

  // Locked tracks keep per-frame remapping and untouched stream accessors.
  ON_CALL(*source, get_audio_samples(0, orc::FrameID{2}))
      .WillByDefault(Return(std::vector<int16_t>{7, 7}));
  EXPECT_EQ(aligned->get_audio_samples(0, orc::FrameID{0}),
            (std::vector<int16_t>{7, 7}));
  EXPECT_EQ(aligned->get_audio_stream_pair_count(0), 0u);
}

TEST(SourceAlignStageTest, PadMode_PrependsFreeRunningSilenceInTimeDomain) {
  orc::SourceAlignStage stage;
  auto source = make_two_track_pal_source();
  ON_CALL(*source, get_audio_stream_samples(1, 0, 2))
      .WillByDefault(Return(std::vector<int16_t>{21, 21, 22, 22}));

  auto padded = align_one(stage, source, "pad_for_alignment", "1+2");
  ASSERT_NE(padded, nullptr);

  // 2 padding frames prepend 3528 silence pairs.
  EXPECT_EQ(padded->get_audio_stream_pair_count(1), 5000u + 3528u);

  // A read straddling the silence/source boundary: 2 silence pairs then the
  // first 2 source pairs.
  EXPECT_EQ(padded->get_audio_stream_samples(1, 3526, 4),
            (std::vector<int16_t>{0, 0, 0, 0, 21, 21, 22, 22}));
}

TEST(SourceAlignStageTest, PadMode_LockedPaddingFramesCarrySilence) {
  orc::SourceAlignStage stage;
  auto source = make_two_track_pal_source();

  auto padded = align_one(stage, source, "pad_for_alignment", "1+2");
  ASSERT_NE(padded, nullptr);

  // ITU-R BT.1700 PAL locked layout: 1764 stereo pairs per frame of silence
  // on padding frames, so the locked track keeps its exact per-frame layout.
  EXPECT_EQ(padded->get_audio_sample_count(0, orc::FrameID{0}), 1764u);
  const auto silence = padded->get_audio_samples(0, orc::FrameID{1});
  ASSERT_EQ(silence.size(), 1764u * 2u);
  EXPECT_TRUE(std::all_of(silence.begin(), silence.end(),
                          [](int16_t s) { return s == 0; }));

  // Free-running tracks have no per-frame samples on padding frames.
  EXPECT_EQ(padded->get_audio_sample_count(1, orc::FrameID{0}), 0u);

  // Real frames shift by the pad count.
  ON_CALL(*source, get_audio_samples(0, orc::FrameID{0}))
      .WillByDefault(Return(std::vector<int16_t>{9, 9}));
  EXPECT_EQ(padded->get_audio_samples(0, orc::FrameID{2}),
            (std::vector<int16_t>{9, 9}));
}

}  // namespace orc_unit_test
