/*
 * File:        stacker_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for StackerStage defaults and parameter validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/stacker/stacker_stage.h"

#include <gtest/gtest.h>
#include <orc/stage/observation_context.h>

#include <algorithm>

#include "../../include/video_frame_representation_artifact_mock.h"
#include "../../mocks/mock_video_frame_representation.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace orc_unit_test {

namespace {
const orc::ParameterDescriptor* find_descriptor(
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::string& name) {
  auto it = std::find_if(descriptors.begin(), descriptors.end(),
                         [&](const orc::ParameterDescriptor& descriptor) {
                           return descriptor.name == name;
                         });

  return it == descriptors.end() ? nullptr : &(*it);
}
}  // namespace

TEST(StackerStageTest, RequiredInputCount_IsOne) {
  orc::StackerStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
}

TEST(StackerStageTest, OutputCount_IsOne) {
  orc::StackerStage stage;
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(StackerStageTest, NodeTypeInfo_HasExpectedMetadata) {
  orc::StackerStage stage;
  auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::MERGER);
  EXPECT_EQ(info.stage_name, "stacker");
  EXPECT_EQ(info.compatible_formats, orc::VideoFormatCompatibility::ALL);
}

TEST(StackerStageTest, Descriptor_DefaultsMatchRuntimeDefaults) {
  orc::StackerStage stage;
  const auto descriptors = stage.get_parameter_descriptors();
  const auto params = stage.get_parameters();

  const auto* mode = find_descriptor(descriptors, "mode");
  const auto* threshold = find_descriptor(descriptors, "smart_threshold");
  const auto* no_diff_dod = find_descriptor(descriptors, "no_diff_dod");
  const auto* passthrough = find_descriptor(descriptors, "passthrough");
  const auto* audio_stacking = find_descriptor(descriptors, "audio_stacking");
  const auto* efm_stacking = find_descriptor(descriptors, "efm_stacking");

  ASSERT_NE(mode, nullptr);
  ASSERT_NE(threshold, nullptr);
  ASSERT_NE(no_diff_dod, nullptr);
  ASSERT_NE(passthrough, nullptr);
  ASSERT_NE(audio_stacking, nullptr);
  ASSERT_NE(efm_stacking, nullptr);

  if (!mode->constraints.default_value.has_value() ||
      !threshold->constraints.default_value.has_value() ||
      !no_diff_dod->constraints.default_value.has_value() ||
      !passthrough->constraints.default_value.has_value() ||
      !audio_stacking->constraints.default_value.has_value() ||
      !efm_stacking->constraints.default_value.has_value()) {
    FAIL() << "Expected all descriptors to have default values";
    return;
  }

  EXPECT_EQ(std::get<std::string>(*mode->constraints.default_value),
            std::get<std::string>(params.at("mode")));
  EXPECT_EQ(std::get<int32_t>(*threshold->constraints.default_value),
            std::get<int32_t>(params.at("smart_threshold")));
  EXPECT_EQ(std::get<bool>(*no_diff_dod->constraints.default_value),
            std::get<bool>(params.at("no_diff_dod")));
  EXPECT_EQ(std::get<bool>(*passthrough->constraints.default_value),
            std::get<bool>(params.at("passthrough")));
  EXPECT_EQ(std::get<std::string>(*audio_stacking->constraints.default_value),
            std::get<std::string>(params.at("audio_stacking")));
  EXPECT_EQ(std::get<std::string>(*efm_stacking->constraints.default_value),
            std::get<std::string>(params.at("efm_stacking")));
}

TEST(StackerStageTest, SetParameters_AcceptsValidStringValues) {
  orc::StackerStage stage;

  const bool result =
      stage.set_parameters({{"mode", std::string("Smart Mean")},
                            {"smart_threshold", static_cast<int32_t>(17)},
                            {"no_diff_dod", true},
                            {"passthrough", true},
                            {"audio_stacking", std::string("Median")},
                            {"efm_stacking", std::string("Disabled")}});
  const auto params = stage.get_parameters();

  EXPECT_TRUE(result);
  EXPECT_EQ(std::get<std::string>(params.at("mode")), "Smart Mean");
  EXPECT_EQ(std::get<int32_t>(params.at("smart_threshold")), 17);
  EXPECT_TRUE(std::get<bool>(params.at("no_diff_dod")));
  EXPECT_TRUE(std::get<bool>(params.at("passthrough")));
  EXPECT_EQ(std::get<std::string>(params.at("audio_stacking")), "Median");
  EXPECT_EQ(std::get<std::string>(params.at("efm_stacking")), "Disabled");
}

TEST(StackerStageTest, SetParameters_AcceptsLegacyIntegerMode) {
  orc::StackerStage stage;

  ASSERT_TRUE(stage.set_parameters({{"mode", int32_t(2)}}));

  EXPECT_EQ(std::get<std::string>(stage.get_parameters().at("mode")),
            "Smart Mean");
}

TEST(StackerStageTest, SetParameters_RejectsInvalidMode) {
  orc::StackerStage stage;
  EXPECT_FALSE(stage.set_parameters({{"mode", std::string("Nope")}}));
}

TEST(StackerStageTest, SetParameters_RejectsThresholdOutsideBounds) {
  orc::StackerStage stage;
  EXPECT_FALSE(stage.set_parameters({{"smart_threshold", int32_t(129)}}));
}

TEST(StackerStageTest, Process_ReturnsNullWhenSourcesEmpty) {
  orc::StackerStage stage;
  EXPECT_EQ(stage.process({}), nullptr);
}

TEST(StackerStageTest, Process_ReturnsOnlySourceInPassthroughMode) {
  orc::StackerStage stage;
  auto source = std::make_shared<MockVideoFrameRepresentation>();

  EXPECT_CALL(*source, has_separate_channels()).WillRepeatedly(Return(false));

  std::vector<std::shared_ptr<const orc::VideoFrameRepresentation>> sources = {
      source};

  const auto result = stage.process(sources);

  EXPECT_EQ(result.get(), source.get());
}

TEST(StackerStageTest, Process_ReturnsWrappedOutputForMultipleSources) {
  orc::StackerStage stage;
  auto src0 = std::make_shared<MockVideoFrameRepresentation>();
  auto src1 = std::make_shared<MockVideoFrameRepresentation>();

  EXPECT_CALL(*src0, has_separate_channels()).WillRepeatedly(Return(false));
  EXPECT_CALL(*src1, has_separate_channels()).WillRepeatedly(Return(false));

  std::vector<std::shared_ptr<const orc::VideoFrameRepresentation>> sources = {
      src0, src1};

  const auto result = stage.process(sources);

  ASSERT_NE(result, nullptr);
  EXPECT_NE(result.get(), src0.get());
  EXPECT_NE(result.get(), src1.get());
}

namespace {

// Minimal in-memory composite source: one frame filled with a constant value.
// NTSC geometry keeps every line at kStackWidth samples.
using sample_type = orc::VideoFrameRepresentation::sample_type;
constexpr size_t kStackWidth = 16;
constexpr size_t kStackHeight = 8;

class FakeConstantSource : public orc::VideoFrameRepresentation {
 public:
  explicit FakeConstantSource(sample_type value)
      : frame_(kStackWidth * kStackHeight, value) {}

  orc::FrameIDRange frame_range() const override {
    return {orc::FrameID{0}, orc::FrameID{0}};
  }
  size_t frame_count() const override { return 1; }
  bool has_frame(orc::FrameID id) const override {
    return id == orc::FrameID{0};
  }

  std::optional<orc::FrameDescriptor> get_frame_descriptor(
      orc::FrameID id) const override {
    if (!has_frame(id)) return std::nullopt;
    orc::FrameDescriptor desc;
    desc.frame_id = id;
    desc.system = orc::VideoSystem::NTSC;
    desc.height = kStackHeight;
    desc.samples_total = frame_.size();
    desc.samples_per_line_nominal = kStackWidth;
    return desc;
  }

  const sample_type* get_frame(orc::FrameID id) const override {
    return has_frame(id) ? frame_.data() : nullptr;
  }
  std::vector<sample_type> get_frame_copy(orc::FrameID id) const override {
    return has_frame(id) ? frame_ : std::vector<sample_type>{};
  }

  std::optional<orc::SourceParameters> get_video_parameters() const override {
    orc::SourceParameters params;
    params.system = orc::VideoSystem::NTSC;
    params.frame_width_nominal = static_cast<int32_t>(kStackWidth);
    params.frame_height = static_cast<int32_t>(kStackHeight);
    params.black_level = 282;
    params.number_of_sequential_frames = 1;
    return params;
  }

 private:
  std::vector<sample_type> frame_;
};

}  // namespace

// Regression: get_line_samples()/get_line() on the stacked representation
// must return the stacked output, never the first source's raw samples
// (observers and analysis sinks read lines through these accessors).
TEST(StackerStageTest, LineReads_ReturnStackedOutputNotFirstSource) {
  orc::StackerStage stage;
  ASSERT_TRUE(stage.set_parameters({{"mode", std::string("Mean")}}));

  auto src0 = std::make_shared<FakeConstantSource>(100);
  auto src1 = std::make_shared<FakeConstantSource>(200);

  const auto stacked = stage.process({src0, src1});
  ASSERT_NE(stacked, nullptr);

  const sample_type* frame = stacked->get_frame(orc::FrameID{0});
  ASSERT_NE(frame, nullptr);

  for (size_t line = 0; line < kStackHeight; ++line) {
    const auto samples = stacked->get_line_samples(orc::FrameID{0}, line);
    ASSERT_EQ(samples.size(), kStackWidth) << "line " << line;
    for (size_t i = 0; i < kStackWidth; ++i) {
      // Mean of 100 and 200 — and definitely not the first source's value.
      EXPECT_EQ(samples[i], 150) << "line " << line << " sample " << i;
      EXPECT_EQ(samples[i], frame[line * kStackWidth + i]);
    }
  }
}

namespace {

// FakeConstantSource with multiple frames and a dropout hint present on every
// frame (line 1, samples 4..11 = flat offset 20, count 8).
class FakeDropoutSource : public FakeConstantSource {
 public:
  FakeDropoutSource(sample_type value, size_t frame_count)
      : FakeConstantSource(value), frame_count_(frame_count) {}

  orc::FrameIDRange frame_range() const override {
    return {orc::FrameID{0}, orc::FrameID{frame_count_ - 1}};
  }
  size_t frame_count() const override { return frame_count_; }
  bool has_frame(orc::FrameID id) const override {
    return id < static_cast<orc::FrameID>(frame_count_);
  }

  std::vector<orc::DropoutRun> get_dropout_hints(
      orc::FrameID id) const override {
    if (!has_frame(id)) return {};
    return {orc::DropoutRun{id, kStackWidth + 4, 8u, 100}};
  }

 private:
  size_t frame_count_;
};

}  // namespace

// Regression: residual dropout runs on the stacked output must carry the
// stacked frame's ID. They used to be emitted with a hard-coded frame_id of
// 0, which broke downstream consumers keying on the field (e.g. dropout_map
// removals).
TEST(StackerStageTest, StackedDropoutHints_CarryStackedFrameId) {
  orc::StackerStage stage;
  ASSERT_TRUE(stage.set_parameters({{"mode", std::string("Mean")}}));

  // Both sources drop out over the same span, so the stacked output has a
  // residual dropout there on every frame.
  auto src0 = std::make_shared<FakeDropoutSource>(100, 2);
  auto src1 = std::make_shared<FakeDropoutSource>(200, 2);

  const auto stacked = stage.process({src0, src1});
  ASSERT_NE(stacked, nullptr);

  const auto hints = stacked->get_dropout_hints(orc::FrameID{1});
  ASSERT_FALSE(hints.empty());
  for (const auto& run : hints) {
    EXPECT_EQ(run.frame_id, orc::FrameID{1});
  }
}

// ── Multi-track audio ────────────────────────────────────────────────────────

namespace {

// One-frame source scaffold for audio stacking tests: frame 0 present with
// no colour-frame index (temporal alignment) and no dropouts by default.
std::shared_ptr<NiceMock<MockVideoFrameRepresentation>>
make_audio_stack_source() {
  auto src = std::make_shared<NiceMock<MockVideoFrameRepresentation>>();
  ON_CALL(*src, has_frame(orc::FrameID{0})).WillByDefault(Return(true));
  orc::FrameDescriptor desc;
  desc.frame_id = orc::FrameID{0};
  desc.colour_frame_index = -1;
  ON_CALL(*src, get_frame_descriptor(orc::FrameID{0}))
      .WillByDefault(Return(desc));
  ON_CALL(*src, frame_range()).WillByDefault(Return(orc::FrameIDRange{0u, 0u}));
  ON_CALL(*src, frame_count()).WillByDefault(Return(1u));
  return src;
}

const orc::AudioTrackDescriptor kLockedTrack{
    "Locked", orc::AudioTrackOrigin::ANALOGUE, true,
    orc::AudioSampleRate{44100, 1}};
const orc::AudioTrackDescriptor kFreeRunningTrack{
    "EFM digital audio", orc::AudioTrackOrigin::EFM, false,
    orc::AudioSampleRate{44100, 1}};

}  // namespace

TEST(StackerStageTest, LockedTracks_StackPerTrackAcrossAllSources) {
  orc::StackerStage stage;  // audio_stacking defaults to Mean
  auto src0 = make_audio_stack_source();
  auto src1 = make_audio_stack_source();
  for (const auto& src : {src0, src1}) {
    ON_CALL(*src, audio_track_count()).WillByDefault(Return(2u));
    ON_CALL(*src, get_audio_track_descriptor(_))
        .WillByDefault(Return(kLockedTrack));
  }
  ON_CALL(*src0, get_audio_samples(0, orc::FrameID{0}))
      .WillByDefault(Return(std::vector<int16_t>{0, 0}));
  ON_CALL(*src1, get_audio_samples(0, orc::FrameID{0}))
      .WillByDefault(Return(std::vector<int16_t>{100, 100}));
  ON_CALL(*src0, get_audio_samples(1, orc::FrameID{0}))
      .WillByDefault(Return(std::vector<int16_t>{10, 20}));
  ON_CALL(*src1, get_audio_samples(1, orc::FrameID{0}))
      .WillByDefault(Return(std::vector<int16_t>{30, 40}));

  const orc::StackedVideoFrameRepresentation stacked({src0, src1}, &stage);

  ASSERT_EQ(stacked.audio_track_count(), 2u);
  // Every locked track common to all inputs is stacked — not just track 0.
  EXPECT_EQ(stacked.get_audio_samples(0, orc::FrameID{0}),
            (std::vector<int16_t>{50, 50}));
  EXPECT_EQ(stacked.get_audio_samples(1, orc::FrameID{0}),
            (std::vector<int16_t>{20, 30}));
}

TEST(StackerStageTest, LockedTrackNotInAllSources_PassesThroughFromBest) {
  orc::StackerStage stage;
  auto src0 = make_audio_stack_source();
  auto src1 = make_audio_stack_source();
  ON_CALL(*src0, audio_track_count()).WillByDefault(Return(2u));
  ON_CALL(*src0, get_audio_track_descriptor(_))
      .WillByDefault(Return(kLockedTrack));
  ON_CALL(*src1, audio_track_count()).WillByDefault(Return(1u));
  ON_CALL(*src1, get_audio_track_descriptor(0))
      .WillByDefault(Return(kLockedTrack));
  ON_CALL(*src0, get_audio_samples(1, orc::FrameID{0}))
      .WillByDefault(Return(std::vector<int16_t>{10, 20}));
  // src1 has more dropouts, so src0 is the best source.
  ON_CALL(*src1, get_dropout_hints(orc::FrameID{0}))
      .WillByDefault(Return(
          std::vector<orc::DropoutRun>{{orc::FrameID{0}, 0u, 10u, 128}}));

  const orc::StackedVideoFrameRepresentation stacked({src0, src1}, &stage);

  // Track 1 exists only in src0: no combining, pass through from best.
  EXPECT_EQ(stacked.get_audio_samples(1, orc::FrameID{0}),
            (std::vector<int16_t>{10, 20}));
}

TEST(StackerStageTest, FreeRunningStreams_PassThroughFromReferenceSource) {
  orc::StackerStage stage;
  auto src0 = make_audio_stack_source();
  auto src1 = make_audio_stack_source();
  for (const auto& src : {src0, src1}) {
    ON_CALL(*src, audio_track_count()).WillByDefault(Return(1u));
    ON_CALL(*src, get_audio_track_descriptor(0))
        .WillByDefault(Return(kFreeRunningTrack));
  }
  ON_CALL(*src0, get_audio_stream_pair_count(0)).WillByDefault(Return(500u));
  ON_CALL(*src0, get_audio_stream_samples(0, 3, 2))
      .WillByDefault(Return(std::vector<int16_t>{5, 5, 6, 6}));
  ON_CALL(*src1, get_audio_stream_pair_count(0)).WillByDefault(Return(999u));

  const orc::StackedVideoFrameRepresentation stacked({src0, src1}, &stage);

  // Never combined: the reference (first audio-carrying) source's stream
  // passes through; other inputs' free-running tracks are discarded.
  EXPECT_EQ(stacked.get_audio_stream_pair_count(0), 500u);
  EXPECT_EQ(stacked.get_audio_stream_samples(0, 3, 2),
            (std::vector<int16_t>{5, 5, 6, 6}));
  // Free-running tracks answer the locked accessors with {}.
  EXPECT_TRUE(stacked.get_audio_samples(0, orc::FrameID{0}).empty());
}

TEST(StackerStageTest, Execute_WarnsWhenFreeRunningTracksAreDiscarded) {
  orc::StackerStage stage;
  auto src0 =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  auto src1 =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*src0, audio_track_count()).WillByDefault(Return(1u));
  ON_CALL(*src0, get_audio_track_descriptor(0))
      .WillByDefault(Return(kLockedTrack));
  ON_CALL(*src1, audio_track_count()).WillByDefault(Return(1u));
  ON_CALL(*src1, get_audio_track_descriptor(0))
      .WillByDefault(Return(kFreeRunningTrack));

  orc::ObservationContext ctx;
  stage.execute({src0, src1}, {}, ctx);

  const auto warning =
      ctx.get(orc::FieldID(0), "stacker", "free_running_tracks_discarded");
  ASSERT_TRUE(warning.has_value());
  EXPECT_NE(std::get<std::string>(*warning).find("input 1 track 0"),
            std::string::npos);
}

TEST(StackerStageTest, Execute_NoWarningWhenAllTracksLocked) {
  orc::StackerStage stage;
  auto src0 =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  auto src1 =
      std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  for (const auto& src : {src0, src1}) {
    ON_CALL(*src, audio_track_count()).WillByDefault(Return(1u));
    ON_CALL(*src, get_audio_track_descriptor(0))
        .WillByDefault(Return(kLockedTrack));
  }

  orc::ObservationContext ctx;
  stage.execute({src0, src1}, {}, ctx);

  EXPECT_FALSE(
      ctx.get(orc::FieldID(0), "stacker", "free_running_tracks_discarded")
          .has_value());
}

}  // namespace orc_unit_test
