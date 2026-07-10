/*
 * File:        audio_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for AudioSinkStage parameter contracts and trigger
 * behavior
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/audio_sink/audio_sink_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../orc/plugins/stages/audio_sink/audio_sink_stage_deps_interface.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_frame_representation_artifact_mock.h"

namespace orc_unit_test {
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

class MockAudioSinkStageDeps : public orc::IAudioSinkStageDeps {
 public:
  MOCK_METHOD(orc::AudioSinkWriteResult, write_audio_wav,
              (const orc::VideoFrameRepresentation* representation,
               const std::string& output_path, size_t track,
               orc::AudioSinkSampleRateMode sample_rate_mode),
              (override));
};

TEST(AudioSinkStageTest, StageInterfaceInvariants_MatchSink) {
  orc::AudioSinkStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
  EXPECT_EQ(stage.output_count(), 0u);
  EXPECT_EQ(stage.get_node_type_info().type, orc::NodeType::SINK);
}

TEST(AudioSinkStageTest, Descriptor_DefaultsOutputPathIsEmptyWav) {
  orc::AudioSinkStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  auto it = std::find_if(descriptors.begin(), descriptors.end(),
                         [](const orc::ParameterDescriptor& d) {
                           return d.name == "output_path";
                         });

  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->type, orc::ParameterType::FILE_PATH);
  EXPECT_EQ(it->file_extension_hint, ".wav");
  if (!it->constraints.default_value.has_value()) {
    FAIL() << "Expected default_value to have a value";
    return;
  }
  EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), "");
}

TEST(AudioSinkStageTest, Trigger_FailsWhenNoInputProvided) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger({}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: Audio sink requires one input (VideoFrameRepresentation)");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(AudioSinkStageTest, Trigger_FailsWhenInputHasNoAudio) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(0));

  const bool result =
      stage.trigger({vfr}, {{"output_path", std::string("ignored.wav")}},
                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: Input VFrameR does not have audio data (no PCM file "
            "specified in source?)");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(AudioSinkStageTest, Trigger_FailsWhenInputIsNotVideoFrameRepresentation) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger({orc::ArtifactPtr{}},
                                    {{"output_path", std::string("out.wav")}},
                                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: Input must be a VideoFrameRepresentation");
}

TEST(AudioSinkStageTest, Trigger_FailsWhenOutputPathMissing) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(1));

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: output_path parameter is required");
}

TEST(AudioSinkStageTest, Trigger_FailsWhenOutputPathIsNotString) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(1));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", static_cast<int32_t>(1)}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: output_path parameter must be a string");
}

TEST(AudioSinkStageTest, Trigger_FailsWhenOutputPathIsEmpty) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(1));

  const bool result = stage.trigger({vfr}, {{"output_path", std::string("")}},
                                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: output_path parameter is empty");
}

TEST(AudioSinkStageTest, Trigger_UsesDepsSeamAndReportsSuccess) {
  orc::AudioSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockAudioSinkStageDeps>>();
  stage.set_deps_override(deps);
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(1));
  // Without track/sample_rate_mode parameters the stage defaults to track 0,
  // locked output.
  EXPECT_CALL(*deps, write_audio_wav(vfr.get(), "out.wav", 0,
                                     orc::AudioSinkSampleRateMode::kLocked))
      .WillOnce(Return(orc::AudioSinkWriteResult{true, 123, ""}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.wav")}}, observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Success: 123 samples written");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(AudioSinkStageTest, Trigger_UsesDepsSeamAndPropagatesFailure) {
  orc::AudioSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockAudioSinkStageDeps>>();
  stage.set_deps_override(deps);
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(1));
  EXPECT_CALL(*deps, write_audio_wav(vfr.get(), "out.wav", 0,
                                     orc::AudioSinkSampleRateMode::kLocked))
      .WillOnce(Return(orc::AudioSinkWriteResult{false, 0, "disk full"}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.wav")}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Error: disk full");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(AudioSinkStageTest, Descriptor_SampleRateModeOfferedForNtscButNotPal) {
  orc::AudioSinkStage stage;

  const auto find_mode = [](const std::vector<orc::ParameterDescriptor>& d) {
    return std::find_if(d.begin(), d.end(),
                        [](const orc::ParameterDescriptor& desc) {
                          return desc.name == "sample_rate_mode";
                        });
  };

  const auto ntsc = stage.get_parameter_descriptors(orc::VideoSystem::NTSC,
                                                    orc::SourceType::Composite);
  auto it = find_mode(ntsc);
  ASSERT_NE(it, ntsc.end());
  EXPECT_EQ(it->type, orc::ParameterType::STRING);
  ASSERT_TRUE(it->constraints.default_value.has_value());
  EXPECT_EQ(std::get<std::string>(*it->constraints.default_value),
            orc::kSampleRateModeLocked);
  EXPECT_THAT(it->constraints.allowed_strings,
              testing::UnorderedElementsAre(orc::kSampleRateModeLocked,
                                            orc::kSampleRateModeFreeRunning));

  const auto pal_m = stage.get_parameter_descriptors(
      orc::VideoSystem::PAL_M, orc::SourceType::Composite);
  EXPECT_NE(find_mode(pal_m), pal_m.end());

  // PAL locked audio is already at 44100 Hz, so the mode is not offered.
  const auto pal = stage.get_parameter_descriptors(orc::VideoSystem::PAL,
                                                   orc::SourceType::Composite);
  EXPECT_EQ(find_mode(pal), pal.end());
}

TEST(AudioSinkStageTest, Trigger_PassesFreeRunningModeToDeps) {
  orc::AudioSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockAudioSinkStageDeps>>();
  stage.set_deps_override(deps);
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(1));
  EXPECT_CALL(*deps,
              write_audio_wav(vfr.get(), "out.wav", 0,
                              orc::AudioSinkSampleRateMode::kFreeRunning))
      .WillOnce(Return(orc::AudioSinkWriteResult{true, 456, ""}));

  const bool result = stage.trigger(
      {vfr},
      {{"output_path", std::string("out.wav")},
       {"sample_rate_mode", std::string(orc::kSampleRateModeFreeRunning)}},
      observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Success: 456 samples written");
}

TEST(AudioSinkStageTest, Descriptor_TrackDefaultsToZeroWithContainerRange) {
  orc::AudioSinkStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  auto it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [](const orc::ParameterDescriptor& d) { return d.name == "track"; });

  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->type, orc::ParameterType::INT32);
  ASSERT_TRUE(it->constraints.default_value.has_value());
  EXPECT_EQ(std::get<int32_t>(*it->constraints.default_value), 0);
  ASSERT_TRUE(it->constraints.min_value.has_value());
  EXPECT_EQ(std::get<int32_t>(*it->constraints.min_value), 0);
  ASSERT_TRUE(it->constraints.max_value.has_value());
  EXPECT_EQ(std::get<int32_t>(*it->constraints.max_value), 15);
}

TEST(AudioSinkStageTest, Trigger_PassesSelectedTrackToDeps) {
  orc::AudioSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockAudioSinkStageDeps>>();
  stage.set_deps_override(deps);
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(3));
  EXPECT_CALL(*deps, write_audio_wav(vfr.get(), "out.wav", 2,
                                     orc::AudioSinkSampleRateMode::kLocked))
      .WillOnce(Return(orc::AudioSinkWriteResult{true, 99, ""}));

  const bool result = stage.trigger({vfr},
                                    {{"output_path", std::string("out.wav")},
                                     {"track", static_cast<int32_t>(2)}},
                                    observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Success: 99 samples written");
}

TEST(AudioSinkStageTest, Trigger_FailsWhenTrackIsOutOfRange) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(1));

  const bool result = stage.trigger({vfr},
                                    {{"output_path", std::string("out.wav")},
                                     {"track", static_cast<int32_t>(16)}},
                                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: track parameter must be between 0 and 15");
}

TEST(AudioSinkStageTest, Trigger_FailsWhenSampleRateModeIsUnknown) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  EXPECT_CALL(*vfr, audio_track_count()).WillOnce(Return(1));

  const bool result =
      stage.trigger({vfr},
                    {{"output_path", std::string("out.wav")},
                     {"sample_rate_mode", std::string("nonsense")}},
                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: sample_rate_mode must be 'locked_44056' or "
            "'free_running_44100'");
}
}  // namespace orc_unit_test
