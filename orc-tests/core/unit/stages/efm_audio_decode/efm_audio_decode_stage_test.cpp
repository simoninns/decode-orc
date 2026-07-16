/*
 * File:        efm_audio_decode_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for EFMAudioDecodeStage (lazy EFM decode, appended
 *              synchronous channel pair, pass-through forwarding)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/efm_audio_decode/efm_audio_decode_stage.h"

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

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

class MockEFMAudioDecodeDeps : public orc::IEFMAudioDecodeDeps {
 public:
  MOCK_METHOD(orc::EFMAudioDecodeResult, decode_to_cache,
              (const orc::VideoFrameRepresentation& representation,
               const orc::EFMAudioDecodeOptions& options,
               (const orc::IEFMAudioDecodeDeps::ProgressFn&)progress),
              (override));
  MOCK_METHOD((std::vector<int16_t>), read_cache_pairs,
              (uint64_t first_pair, uint32_t pair_count), (const, override));
  MOCK_METHOD(bool, write_synchronous_cache,
              (const std::vector<int32_t>& samples), (override));
  MOCK_METHOD((std::vector<int32_t>), read_synchronous_pairs,
              (uint64_t first_pair, uint32_t pair_count), (const, override));
};

const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descs,
    const std::string& name) {
  auto it = std::find_if(
      descs.begin(), descs.end(),
      [&](const orc::ParameterDescriptor& d) { return d.name == name; });
  return it == descs.end() ? nullptr : &(*it);
}

// Gives the mock source a video system and frame count so the lazy decode
// path can compute the cadence.
void configure_video(NiceMock<MockVideoFrameRepresentationArtifact>& vfr,
                     orc::VideoSystem system, size_t frame_count) {
  orc::SourceParameters params;
  params.system = system;
  ON_CALL(vfr, get_video_parameters()).WillByDefault(Return(params));
  ON_CALL(vfr, frame_count()).WillByDefault(Return(frame_count));
}

// Runs the stage over a mock source with |source_pairs| existing audio
// channel pairs and returns the output representation.
std::shared_ptr<const orc::VideoFrameRepresentation> make_output(
    orc::EFMAudioDecodeStage& stage,
    std::shared_ptr<NiceMock<MockVideoFrameRepresentationArtifact>> vfr,
    size_t source_pairs) {
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(source_pairs));
  orc::ObservationContext ctx;
  auto outputs = stage.execute({vfr}, {}, ctx);
  EXPECT_EQ(outputs.size(), 1u);
  auto output = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
      outputs[0]);
  EXPECT_NE(output, nullptr);
  return output;
}

TEST(EFMAudioDecodeStageTest, NodeTypeInfo_ReportsTransformWithOneInput) {
  orc::EFMAudioDecodeStage stage;
  const auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "efm_audio_decode");
  EXPECT_EQ(info.min_inputs, 1u);
  EXPECT_EQ(info.max_inputs, 1u);
  EXPECT_EQ(stage.required_input_count(), 1u);
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(EFMAudioDecodeStageTest, Descriptors_DefaultsRoundTripThroughSetGet) {
  orc::EFMAudioDecodeStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  const auto* timecodes = find_descriptor(descriptors, "no_timecodes");
  const auto* concealment =
      find_descriptor(descriptors, "no_audio_concealment");
  ASSERT_NE(timecodes, nullptr);
  ASSERT_NE(concealment, nullptr);
  EXPECT_EQ(timecodes->type, orc::ParameterType::BOOL);
  EXPECT_EQ(concealment->type, orc::ParameterType::BOOL);
  ASSERT_TRUE(timecodes->constraints.default_value.has_value());
  ASSERT_TRUE(concealment->constraints.default_value.has_value());
  EXPECT_FALSE(std::get<bool>(*timecodes->constraints.default_value));
  EXPECT_FALSE(std::get<bool>(*concealment->constraints.default_value));

  // Defaults are reflected by get_parameters().
  auto params = stage.get_parameters();
  EXPECT_FALSE(std::get<bool>(params.at("no_timecodes")));
  EXPECT_FALSE(std::get<bool>(params.at("no_audio_concealment")));

  // report is a BOOL checkbox defaulting to off; report_path is a FILE_PATH
  // that depends on the checkbox being enabled and defaults to empty.
  const auto* report = find_descriptor(descriptors, "report");
  const auto* report_path = find_descriptor(descriptors, "report_path");
  ASSERT_NE(report, nullptr);
  ASSERT_NE(report_path, nullptr);
  EXPECT_EQ(report->type, orc::ParameterType::BOOL);
  ASSERT_TRUE(report->constraints.default_value.has_value());
  EXPECT_FALSE(std::get<bool>(*report->constraints.default_value));
  EXPECT_EQ(report_path->type, orc::ParameterType::FILE_PATH);
  ASSERT_TRUE(report_path->constraints.default_value.has_value());
  EXPECT_TRUE(
      std::get<std::string>(*report_path->constraints.default_value).empty());
  // report_path is gated on report == true.
  ASSERT_TRUE(report_path->constraints.depends_on.has_value());
  EXPECT_EQ(report_path->constraints.depends_on->parameter_name, "report");
  EXPECT_THAT(report_path->constraints.depends_on->required_values,
              ::testing::ElementsAre("true"));

  EXPECT_FALSE(std::get<bool>(params.at("report")));
  EXPECT_TRUE(std::get<std::string>(params.at("report_path")).empty());

  // Round-trip non-default values.
  EXPECT_TRUE(
      stage.set_parameters({{"no_timecodes", true},
                            {"no_audio_concealment", true},
                            {"report", true},
                            {"report_path", std::string("/tmp/decode.txt")}}));
  params = stage.get_parameters();
  EXPECT_TRUE(std::get<bool>(params.at("no_timecodes")));
  EXPECT_TRUE(std::get<bool>(params.at("no_audio_concealment")));
  EXPECT_TRUE(std::get<bool>(params.at("report")));
  EXPECT_EQ(std::get<std::string>(params.at("report_path")), "/tmp/decode.txt");
}

TEST(EFMAudioDecodeStageTest, Execute_ThrowsOnMissingOrNonVfrInput) {
  orc::EFMAudioDecodeStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({}, {}, ctx), orc::DAGExecutionError);
}

TEST(EFMAudioDecodeStageTest, Execute_ThrowsWhenPairCapReached) {
  orc::EFMAudioDecodeStage stage;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count())
      .WillByDefault(Return(orc::kMaxAudioChannelPairs));

  // Cap-exceeded input errors out; no pair is appended.
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({vfr}, {}, ctx), orc::DAGExecutionError);
}

TEST(EFMAudioDecodeStageTest, Execute_AppendsEfmChannelPair) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  const orc::AudioChannelPairDescriptor source_desc{"Analogue",
                                                    orc::AudioOrigin::ANALOGUE};
  ON_CALL(*vfr, get_audio_channel_pair_descriptor(0))
      .WillByDefault(Return(source_desc));

  auto output = make_output(stage, vfr, 1);

  EXPECT_EQ(output->audio_channel_pair_count(), 2u);
  EXPECT_TRUE(output->has_audio());

  // Source pair descriptor forwards untouched.
  const auto fwd = output->get_audio_channel_pair_descriptor(0);
  ASSERT_TRUE(fwd.has_value());
  EXPECT_EQ(fwd->name, "Analogue");
  EXPECT_EQ(fwd->origin, orc::AudioOrigin::ANALOGUE);

  // Appended pair is the EFM digital audio channel pair — and none of this
  // metadata access triggers a decode (StrictMock deps).
  const auto efm = output->get_audio_channel_pair_descriptor(1);
  ASSERT_TRUE(efm.has_value());
  EXPECT_EQ(efm->name, "EFM digital audio");
  EXPECT_EQ(efm->origin, orc::AudioOrigin::EFM);
}

TEST(EFMAudioDecodeStageTest, OutOfRangePair_AnswersEmptyAndNullopt) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  auto output = make_output(stage, vfr, 1);

  // One source pair plus the appended EFM pair — index 2 is out of range.
  EXPECT_FALSE(output->get_audio_channel_pair_descriptor(2).has_value());
  EXPECT_TRUE(output->get_audio_samples(2, orc::FrameID(0)).empty());
}

TEST(EFMAudioDecodeStageTest, AudioAccess_ForwardsToSourcePairs) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  const std::vector<int32_t> frame_samples{1 << 8, -(1 << 8), 2 << 8,
                                           -(2 << 8)};
  ON_CALL(*vfr, get_audio_samples(0, orc::FrameID(7)))
      .WillByDefault(Return(frame_samples));

  auto output = make_output(stage, vfr, 1);

  // Source-pair sample access forwards and never touches the decode deps
  // (StrictMock).
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(7)), frame_samples);
}

TEST(EFMAudioDecodeStageTest, SampleAccess_DecodesResamplesAndCachesOnce_Pal) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  constexpr size_t kFrames = 2;
  configure_video(*vfr, orc::VideoSystem::PAL, kFrames);
  auto output = make_output(stage, vfr, 1);

  // Raw decoded CD audio: 2 PAL frames' worth at 44.1 kHz (1764 pairs per
  // frame). Zero-valued samples convert to zero exactly through the widening
  // and the linear SoXR resampler, making the cached stream deterministic.
  constexpr uint64_t kRawPairs = 44100 / 25 * kFrames;
  const std::vector<int16_t> raw(kRawPairs * 2, 0);

  // No decode so far (StrictMock would have failed). The first sample access
  // triggers exactly one decode → raw read → convert → cache-write sequence.
  EXPECT_CALL(*deps, decode_to_cache(_, _, _))
      .Times(1)
      .WillOnce(Return(orc::EFMAudioDecodeResult{true, "", kRawPairs}));
  EXPECT_CALL(*deps, read_cache_pairs(0, static_cast<uint32_t>(kRawPairs)))
      .Times(1)
      .WillOnce(Return(raw));
  EXPECT_CALL(*deps, write_synchronous_cache(_))
      .Times(1)
      .WillOnce([](const std::vector<int32_t>& samples) {
        // The converted stream is cadence-aligned: exactly
        // audio_pair_offset(frame_count) = 2 × 1920 stereo pairs, silent.
        EXPECT_EQ(samples.size(),
                  orc::audio_pair_offset(kFrames, orc::VideoSystem::PAL) * 2);
        EXPECT_TRUE(std::all_of(samples.begin(), samples.end(),
                                [](int32_t v) { return v == 0; }));
        return true;
      });

  // Frame 0 is served from pair offset 0; frame 1 from pair offset 1920.
  const std::vector<int32_t> frame0_block(1920 * 2, 5 << 8);
  const std::vector<int32_t> frame1_block(1920 * 2, 42 << 8);
  EXPECT_CALL(*deps, read_synchronous_pairs(0, 1920))
      .WillOnce(Return(frame0_block));
  EXPECT_CALL(*deps, read_synchronous_pairs(1920, 1920))
      .WillOnce(Return(frame1_block));

  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(0)), frame0_block);
  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(1)), frame1_block);
}

TEST(EFMAudioDecodeStageTest, Prime_RunsDecodeOnceAndForwardsProgress) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  constexpr size_t kFrames = 2;
  configure_video(*vfr, orc::VideoSystem::PAL, kFrames);
  auto output = make_output(stage, vfr, 0);

  constexpr uint64_t kRawPairs = 44100 / 25 * kFrames;
  // The deps invoke the forwarded progress callback so we can assert it reaches
  // the caller, then fail the decode to keep the test focused on the priming
  // plumbing (a failed decode still counts as "decoded once").
  EXPECT_CALL(*deps, decode_to_cache(_, _, _))
      .Times(1)
      .WillOnce([](const orc::VideoFrameRepresentation&,
                   const orc::EFMAudioDecodeOptions&,
                   const orc::IEFMAudioDecodeDeps::ProgressFn& progress) {
        if (progress) progress(1, kFrames, "Decoding EFM audio...");
        return orc::EFMAudioDecodeResult{false, "stop here", 0};
      });

  bool progress_seen = false;
  output->prime_audio_decode(
      [&](uint64_t done, uint64_t total, const std::string& message) {
        progress_seen = true;
        EXPECT_EQ(done, 1u);
        EXPECT_EQ(total, kFrames);
        EXPECT_FALSE(message.empty());
      });
  EXPECT_TRUE(progress_seen);

  // Priming already ran the (single) decode; a later sample access must not
  // trigger a second one — StrictMock would fail if decode_to_cache ran again.
  const auto samples = output->get_audio_samples(0, orc::FrameID(0));
  EXPECT_EQ(samples.size(), 1920u * 2);  // silence, cadence-sized
}

// A minimal pass-through wrapper standing in for a stage (e.g.
// audio_channel_map) sitting between the EFM decode and the sink. Priming must
// forward through it.
class PassThroughWrapper : public orc::VideoFrameRepresentationWrapper {
 public:
  explicit PassThroughWrapper(
      std::shared_ptr<const orc::VideoFrameRepresentation> source)
      : orc::VideoFrameRepresentationWrapper(std::move(source)) {}
  std::vector<orc::VideoFrameRepresentation::sample_type> get_frame_copy(
      orc::FrameID) const override {
    return {};
  }
  const orc::VideoFrameRepresentation::sample_type* get_frame(
      orc::FrameID) const override {
    return nullptr;
  }
};

TEST(EFMAudioDecodeStageTest, Prime_ForwardsThroughInterveningWrapper) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  constexpr size_t kFrames = 2;
  configure_video(*vfr, orc::VideoSystem::PAL, kFrames);
  auto output = make_output(stage, vfr, 0);

  // Wrap the EFM representation the way a downstream stage would, so the sink's
  // direct input is the wrapper — not the EFM representation itself.
  auto wrapped = std::make_shared<PassThroughWrapper>(output);

  EXPECT_CALL(*deps, decode_to_cache(_, _, _))
      .Times(1)
      .WillOnce([](const orc::VideoFrameRepresentation&,
                   const orc::EFMAudioDecodeOptions&,
                   const orc::IEFMAudioDecodeDeps::ProgressFn& progress) {
        if (progress) progress(1, kFrames, "Decoding EFM audio...");
        return orc::EFMAudioDecodeResult{false, "stop here", 0};
      });

  // Priming the wrapper must reach the nested EFM decode through the chain.
  bool progress_seen = false;
  wrapped->prime_audio_decode(
      [&](uint64_t, uint64_t, const std::string&) { progress_seen = true; });
  EXPECT_TRUE(progress_seen);
}

TEST(EFMAudioDecodeStageTest, SampleAccess_ServesNtscCadenceSizedBlocks) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  constexpr size_t kFrames = 5;
  configure_video(*vfr, orc::VideoSystem::NTSC, kFrames);
  auto output = make_output(stage, vfr, 0);

  constexpr uint64_t kRawPairs = 7357;  // ≈ 5 NTSC frames at 44.1 kHz
  EXPECT_CALL(*deps, decode_to_cache(_, _, _))
      .Times(1)
      .WillOnce(Return(orc::EFMAudioDecodeResult{true, "", kRawPairs}));
  EXPECT_CALL(*deps, read_cache_pairs(0, static_cast<uint32_t>(kRawPairs)))
      .Times(1)
      .WillOnce(Return(std::vector<int16_t>(kRawPairs * 2, 0)));
  EXPECT_CALL(*deps, write_synchronous_cache(_))
      .Times(1)
      .WillOnce([](const std::vector<int32_t>& samples) {
        // SMPTE 272M-1994 §14.3: one full audio frame sequence = 8008 pairs.
        EXPECT_EQ(samples.size(), 8008u * 2);
        return true;
      });

  // SMPTE 272M-1994 §14.3 Table 1 cadence: frame 0 carries 1602 pairs from
  // offset 0; frame 1 carries 1601 pairs from offset 1602.
  EXPECT_CALL(*deps, read_synchronous_pairs(0, 1602))
      .WillOnce(Return(std::vector<int32_t>(1602 * 2, 7)));
  EXPECT_CALL(*deps, read_synchronous_pairs(1602, 1601))
      .WillOnce(Return(std::vector<int32_t>(1601 * 2, 9)));

  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)).size(), 1602u * 2);
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(1)).size(), 1601u * 2);
}

TEST(EFMAudioDecodeStageTest, ShortCacheRead_IsSilencePaddedToCadenceSize) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  configure_video(*vfr, orc::VideoSystem::PAL, 1);
  auto output = make_output(stage, vfr, 0);

  EXPECT_CALL(*deps, decode_to_cache(_, _, _))
      .WillOnce(Return(orc::EFMAudioDecodeResult{true, "", 10}));
  EXPECT_CALL(*deps, read_cache_pairs(0, 10))
      .WillOnce(Return(std::vector<int16_t>(20, 0)));
  EXPECT_CALL(*deps, write_synchronous_cache(_)).WillOnce(Return(true));

  // The cache answers a single stereo pair; the served block must still be
  // exactly audio_pairs_in_frame = 1920 pairs, silence-padded at the tail.
  EXPECT_CALL(*deps, read_synchronous_pairs(0, 1920))
      .WillOnce(Return(std::vector<int32_t>{123 << 8, -(123 << 8)}));

  const auto samples = output->get_audio_samples(0, orc::FrameID(0));
  ASSERT_EQ(samples.size(), 1920u * 2);
  EXPECT_EQ(samples[0], 123 << 8);
  EXPECT_EQ(samples[1], -(123 << 8));
  EXPECT_TRUE(std::all_of(samples.begin() + 2, samples.end(),
                          [](int32_t v) { return v == 0; }));
}

TEST(EFMAudioDecodeStageTest, FailedDecode_ServesSilenceAndDecodesOnlyOnce) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  configure_video(*vfr, orc::VideoSystem::PAL, 3);
  auto output = make_output(stage, vfr, 0);

  // A failed decode (e.g. an EFM data disc) is attempted only once and
  // leaves the appended pair silent — cadence-sized zero blocks, and the
  // synchronous cache is never touched (StrictMock).
  EXPECT_CALL(*deps, decode_to_cache(_, _, _))
      .Times(1)
      .WillOnce(
          Return(orc::EFMAudioDecodeResult{false, "no EFM t-values found", 0}));

  for (int repeat = 0; repeat < 2; ++repeat) {
    const auto samples = output->get_audio_samples(0, orc::FrameID(0));
    ASSERT_EQ(samples.size(), 1920u * 2);
    EXPECT_TRUE(std::all_of(samples.begin(), samples.end(),
                            [](int32_t v) { return v == 0; }));
  }
}

TEST(EFMAudioDecodeStageTest, Parameters_ArePassedToDecode) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(0u));
  configure_video(*vfr, orc::VideoSystem::PAL, 1);

  orc::ObservationContext ctx;
  auto outputs =
      stage.execute({vfr},
                    {{"no_timecodes", true},
                     {"no_audio_concealment", true},
                     {"report", true},
                     {"report_path", std::string("/tmp/decode.txt")}},
                    ctx);
  auto output = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
      outputs[0]);
  ASSERT_NE(output, nullptr);

  EXPECT_CALL(*deps, decode_to_cache(_, _, _))
      .WillOnce([](const orc::VideoFrameRepresentation&,
                   const orc::EFMAudioDecodeOptions& options,
                   const orc::IEFMAudioDecodeDeps::ProgressFn&) {
        EXPECT_TRUE(options.no_timecodes);
        EXPECT_TRUE(options.no_audio_concealment);
        EXPECT_EQ(options.report_path, "/tmp/decode.txt");
        // Failing the decode keeps the test focused on option forwarding.
        return orc::EFMAudioDecodeResult{false, "stop here", 0};
      });
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)).size(), 1920u * 2);
}

TEST(EFMAudioDecodeStageTest, ReportPath_IsSuppressedWhenCheckboxOff) {
  orc::EFMAudioDecodeStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMAudioDecodeDeps>>();
  stage.set_deps_override(deps);

  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(0u));
  configure_video(*vfr, orc::VideoSystem::PAL, 1);

  // A path is set but the report checkbox is left off: no report is written.
  orc::ObservationContext ctx;
  auto outputs = stage.execute(
      {vfr},
      {{"report", false}, {"report_path", std::string("/tmp/decode.txt")}},
      ctx);
  auto output = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
      outputs[0]);
  ASSERT_NE(output, nullptr);

  EXPECT_CALL(*deps, decode_to_cache(_, _, _))
      .WillOnce([](const orc::VideoFrameRepresentation&,
                   const orc::EFMAudioDecodeOptions& options,
                   const orc::IEFMAudioDecodeDeps::ProgressFn&) {
        EXPECT_TRUE(options.report_path.empty());
        return orc::EFMAudioDecodeResult{false, "stop here", 0};
      });
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)).size(), 1920u * 2);
}

}  // namespace
}  // namespace orc_unit_test
