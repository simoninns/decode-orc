/*
 * File:        audio_import_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for AudioImportStage (WAV validation, ingest
 *              conversion to synchronous 48 kHz 24-bit channel pairs, cap
 *              enforcement)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/audio_import/audio_import_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <orc/stage/observation_context.h>

#include <algorithm>

#include "../../include/video_frame_representation_artifact_mock.h"

namespace orc_unit_test {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class MockAudioImportDeps : public orc::IAudioImportDeps {
 public:
  MOCK_METHOD(orc::WavProbeResult, open, (const std::string& wav_path),
              (override));
  MOCK_METHOD((std::vector<int16_t>), read_all_pairs_16, (), (const, override));
  MOCK_METHOD((std::vector<int32_t>), read_all_pairs_24, (), (const, override));
};

const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descs,
    const std::string& name) {
  auto it = std::find_if(
      descs.begin(), descs.end(),
      [&](const orc::ParameterDescriptor& d) { return d.name == name; });
  return it == descs.end() ? nullptr : &(*it);
}

// PAL source with 2 frames and one existing audio channel pair.
std::shared_ptr<NiceMock<MockVideoFrameRepresentationArtifact>>
make_pal_source() {
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  orc::SourceParameters params;
  params.system = orc::VideoSystem::PAL;
  ON_CALL(*vfr, get_video_parameters()).WillByDefault(Return(params));
  ON_CALL(*vfr, frame_count()).WillByDefault(Return(2u));
  ON_CALL(*vfr, frame_range())
      .WillByDefault(
          Return(orc::FrameIDRange{orc::FrameID(0), orc::FrameID(1)}));
  ON_CALL(*vfr, has_frame(_)).WillByDefault(Return(true));
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(1u));
  return vfr;
}

std::shared_ptr<const orc::VideoFrameRepresentation> run(
    orc::AudioImportStage& stage, orc::ArtifactPtr input,
    const std::map<std::string, orc::ParameterValue>& params) {
  orc::ObservationContext ctx;
  auto outputs = stage.execute({std::move(input)}, params, ctx);
  EXPECT_EQ(outputs.size(), 1u);
  auto output = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
      outputs[0]);
  EXPECT_NE(output, nullptr);
  return output;
}

TEST(AudioImportStageTest, NodeTypeInfo_ReportsTransformWithOneInput) {
  orc::AudioImportStage stage;
  const auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "audio_import");
  EXPECT_EQ(stage.required_input_count(), 1u);
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(AudioImportStageTest, Descriptors_DefaultsRoundTripThroughSetGet) {
  orc::AudioImportStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  const auto* wav_path = find_descriptor(descriptors, "wav_path");
  const auto* pair_name = find_descriptor(descriptors, "pair_name");
  ASSERT_NE(wav_path, nullptr);
  ASSERT_NE(pair_name, nullptr);
  EXPECT_EQ(wav_path->type, orc::ParameterType::FILE_PATH);
  EXPECT_TRUE(wav_path->constraints.required);
  EXPECT_EQ(wav_path->file_extension_hint, ".wav");

  // The lock_mode parameter is gone: audio is always synchronous 48 kHz.
  EXPECT_EQ(find_descriptor(descriptors, "lock_mode"), nullptr);
  EXPECT_FALSE(stage.set_parameters({{"lock_mode", std::string("auto")}}));

  EXPECT_TRUE(stage.set_parameters({{"wav_path", std::string("/tmp/a.wav")},
                                    {"pair_name", std::string("Commentary")}}));
  const auto params = stage.get_parameters();
  EXPECT_EQ(std::get<std::string>(params.at("wav_path")), "/tmp/a.wav");
  EXPECT_EQ(std::get<std::string>(params.at("pair_name")), "Commentary");
}

TEST(AudioImportStageTest, Execute_ThrowsWhenWavPathUnset) {
  orc::AudioImportStage stage;
  auto vfr = make_pal_source();
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({vfr}, {}, ctx), orc::DAGExecutionError);
}

TEST(AudioImportStageTest, Execute_ThrowsWhenPairCapReached) {
  orc::AudioImportStage stage;
  auto vfr = make_pal_source();
  ON_CALL(*vfr, audio_channel_pair_count())
      .WillByDefault(Return(orc::kMaxAudioChannelPairs));

  orc::ObservationContext ctx;
  EXPECT_THROW(
      stage.execute({vfr}, {{"wav_path", std::string("/tmp/a.wav")}}, ctx),
      orc::DAGExecutionError);
}

TEST(AudioImportStageTest, Execute_ThrowsWhenWavInvalid) {
  orc::AudioImportStage stage;
  auto deps = std::make_shared<NiceMock<MockAudioImportDeps>>();
  stage.set_deps_override(deps);
  ON_CALL(*deps, open(_))
      .WillByDefault(Return(orc::WavProbeResult{false, "not stereo", 0, 0, 0}));

  auto vfr = make_pal_source();
  orc::ObservationContext ctx;
  EXPECT_THROW(
      stage.execute({vfr}, {{"wav_path", std::string("/tmp/a.wav")}}, ctx),
      orc::DAGExecutionError);
}

TEST(AudioImportStageTest, Execute_ThrowsWhenVideoSystemUnknown) {
  orc::AudioImportStage stage;
  auto deps = std::make_shared<NiceMock<MockAudioImportDeps>>();
  stage.set_deps_override(deps);
  ON_CALL(*deps, open(_))
      .WillByDefault(Return(orc::WavProbeResult{true, "", 48000, 16, 1000}));

  auto vfr = make_pal_source();
  ON_CALL(*vfr, get_video_parameters())
      .WillByDefault(Return(std::optional<orc::SourceParameters>()));

  orc::ObservationContext ctx;
  EXPECT_THROW(
      stage.execute({vfr}, {{"wav_path", std::string("/tmp/a.wav")}}, ctx),
      orc::DAGExecutionError);
}

TEST(AudioImportStageTest, Import_Converts16BitWavToSynchronousPairs) {
  orc::AudioImportStage stage;
  auto deps = std::make_shared<NiceMock<MockAudioImportDeps>>();
  stage.set_deps_override(deps);

  // A 48000 Hz 16-bit WAV of exactly 2 frames × 1920 PAL pairs passes
  // through the resampler unchanged; only the 16→24-bit widening applies.
  constexpr size_t kPairs = 2u * 1920u;
  std::vector<int16_t> raw(kPairs * 2);
  for (size_t i = 0; i < raw.size(); ++i) {
    raw[i] = static_cast<int16_t>(static_cast<int>(i % 1000) - 500);
  }
  ON_CALL(*deps, open(_))
      .WillByDefault(Return(orc::WavProbeResult{true, "", 48000, 16, kPairs}));
  // The whole WAV is read and converted once, on first audio access.
  EXPECT_CALL(*deps, read_all_pairs_16()).Times(1).WillOnce(Return(raw));
  EXPECT_CALL(*deps, read_all_pairs_24()).Times(0);

  auto vfr = make_pal_source();
  auto output =
      run(stage, vfr, {{"wav_path", std::string("/tmp/My Pair.wav")}});

  ASSERT_EQ(output->audio_channel_pair_count(), 2u);
  const auto desc = output->get_audio_channel_pair_descriptor(1);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->name, "My Pair");  // file stem when pair_name is empty
  EXPECT_EQ(desc->origin, orc::AudioOrigin::IMPORTED);

  const auto frame0 = output->get_audio_samples(1, orc::FrameID(0));
  const auto frame1 = output->get_audio_samples(1, orc::FrameID(1));
  ASSERT_EQ(frame0.size(), 1920u * 2u);
  ASSERT_EQ(frame1.size(), 1920u * 2u);
  // Widening is a 256× scale (<< 8, sign-preserving).
  for (size_t i = 0; i < frame0.size(); ++i) {
    EXPECT_EQ(frame0[i], static_cast<int32_t>(raw[i]) * 256) << "index " << i;
  }
  EXPECT_EQ(frame1.front(), static_cast<int32_t>(raw[1920u * 2u]) * 256);
  EXPECT_EQ(frame1.back(), static_cast<int32_t>(raw.back()) * 256);
}

TEST(AudioImportStageTest, Import_Converts24BitInputPath) {
  orc::AudioImportStage stage;
  auto deps = std::make_shared<NiceMock<MockAudioImportDeps>>();
  stage.set_deps_override(deps);

  // 24-bit material is already in the int32 carrier: no widening, and the
  // 48000 Hz rate passes through the resampler unchanged. Short material is
  // silence-padded to the cadence size.
  const std::vector<int32_t> raw{100000, -100000, 8388607, -8388608};
  ON_CALL(*deps, open(_))
      .WillByDefault(Return(orc::WavProbeResult{true, "", 48000, 24, 2}));
  EXPECT_CALL(*deps, read_all_pairs_24()).Times(1).WillOnce(Return(raw));
  EXPECT_CALL(*deps, read_all_pairs_16()).Times(0);

  auto vfr = make_pal_source();
  auto output = run(stage, vfr, {{"wav_path", std::string("/tmp/a.wav")}});

  const auto frame0 = output->get_audio_samples(1, orc::FrameID(0));
  ASSERT_EQ(frame0.size(), 1920u * 2u);
  EXPECT_EQ(frame0[0], 100000);
  EXPECT_EQ(frame0[1], -100000);
  EXPECT_EQ(frame0[2], 8388607);
  EXPECT_EQ(frame0[3], -8388608);
  EXPECT_EQ(frame0[4], 0);  // silence-padded past end of material

  const auto frame1 = output->get_audio_samples(1, orc::FrameID(1));
  ASSERT_EQ(frame1.size(), 1920u * 2u);
  EXPECT_TRUE(std::all_of(frame1.begin(), frame1.end(),
                          [](int32_t s) { return s == 0; }));
}

TEST(AudioImportStageTest, Import_ResamplesNonNativeRateToCadenceBlocks) {
  orc::AudioImportStage stage;
  auto deps = std::make_shared<NiceMock<MockAudioImportDeps>>();
  stage.set_deps_override(deps);

  // A 44100 Hz WAV is resampled to 48000 Hz on ingest; every frame block is
  // cadence-sized regardless of the source length.
  ON_CALL(*deps, open(_))
      .WillByDefault(Return(orc::WavProbeResult{true, "", 44100, 16, 1000}));
  ON_CALL(*deps, read_all_pairs_16())
      .WillByDefault(Return(std::vector<int16_t>(1000 * 2, 100)));

  auto vfr = make_pal_source();
  auto output = run(stage, vfr, {{"wav_path", std::string("/tmp/a.wav")}});

  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(0)).size(), 1920u * 2u);
  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(1)).size(), 1920u * 2u);
}

TEST(AudioImportStageTest, Import_SegmentsNtscCadence) {
  orc::AudioImportStage stage;
  auto deps = std::make_shared<NiceMock<MockAudioImportDeps>>();
  stage.set_deps_override(deps);

  // NTSC audio frame sequence (SMPTE 272M-1994 §14.3): frames 0 and 1 carry
  // 1602 and 1601 stereo pairs.
  constexpr size_t kPairs = 1602u + 1601u;
  ON_CALL(*deps, open(_))
      .WillByDefault(Return(orc::WavProbeResult{true, "", 48000, 16, kPairs}));
  ON_CALL(*deps, read_all_pairs_16())
      .WillByDefault(Return(std::vector<int16_t>(kPairs * 2, 7)));

  auto vfr = make_pal_source();
  orc::SourceParameters params;
  params.system = orc::VideoSystem::NTSC;
  ON_CALL(*vfr, get_video_parameters()).WillByDefault(Return(params));

  auto output = run(stage, vfr, {{"wav_path", std::string("/tmp/a.wav")}});

  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(0)).size(), 1602u * 2u);
  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(1)).size(), 1601u * 2u);
}

TEST(AudioImportStageTest, OutOfRangePair_ReturnsEmptyAndNullopt) {
  orc::AudioImportStage stage;
  auto deps = std::make_shared<NiceMock<MockAudioImportDeps>>();
  stage.set_deps_override(deps);
  ON_CALL(*deps, open(_))
      .WillByDefault(Return(orc::WavProbeResult{true, "", 48000, 16, 4}));

  auto vfr = make_pal_source();
  auto output = run(stage, vfr, {{"wav_path", std::string("/tmp/a.wav")}});

  ASSERT_EQ(output->audio_channel_pair_count(), 2u);
  EXPECT_FALSE(output->get_audio_channel_pair_descriptor(2).has_value());
  EXPECT_TRUE(output->get_audio_samples(2, orc::FrameID(0)).empty());
}

TEST(AudioImportStageTest, SourcePairs_ForwardUntouched) {
  orc::AudioImportStage stage;
  auto deps = std::make_shared<NiceMock<MockAudioImportDeps>>();
  stage.set_deps_override(deps);
  ON_CALL(*deps, open(_))
      .WillByDefault(Return(orc::WavProbeResult{true, "", 48000, 16, 4}));

  auto vfr = make_pal_source();
  const orc::AudioChannelPairDescriptor source_desc{"Analogue",
                                                    orc::AudioOrigin::ANALOGUE};
  ON_CALL(*vfr, get_audio_channel_pair_descriptor(0))
      .WillByDefault(Return(source_desc));
  ON_CALL(*vfr, get_audio_samples(0, orc::FrameID(0)))
      .WillByDefault(Return(std::vector<int32_t>{1, 2}));

  auto output = run(stage, vfr,
                    {{"wav_path", std::string("/tmp/a.wav")},
                     {"pair_name", std::string("Commentary")}});

  const auto fwd = output->get_audio_channel_pair_descriptor(0);
  ASSERT_TRUE(fwd.has_value());
  EXPECT_EQ(fwd->name, "Analogue");
  EXPECT_EQ(fwd->origin, orc::AudioOrigin::ANALOGUE);
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)),
            (std::vector<int32_t>{1, 2}));

  const auto desc = output->get_audio_channel_pair_descriptor(1);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->name, "Commentary");  // pair_name overrides the file stem
}

}  // namespace
}  // namespace orc_unit_test
