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
#include <orc/stage/audio_channel_pair.h>

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
               const std::string& output_path, size_t pair),
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

TEST(AudioSinkStageTest, Descriptor_SampleRateModeIsNotOffered) {
  // Pipeline audio is uniformly 48 kHz synchronous; the legacy
  // sample_rate_mode export parameter no longer exists for any system.
  orc::AudioSinkStage stage;

  for (const auto system : {orc::VideoSystem::PAL, orc::VideoSystem::NTSC,
                            orc::VideoSystem::PAL_M}) {
    const auto descriptors =
        stage.get_parameter_descriptors(system, orc::SourceType::Composite);
    const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                 [](const orc::ParameterDescriptor& d) {
                                   return d.name == "sample_rate_mode";
                                 });
    EXPECT_EQ(it, descriptors.end());
  }
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

  EXPECT_CALL(*vfr, audio_channel_pair_count()).WillOnce(Return(0));

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
  EXPECT_CALL(*vfr, audio_channel_pair_count()).WillOnce(Return(1));

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: output_path parameter is required");
}

TEST(AudioSinkStageTest, Trigger_FailsWhenOutputPathIsNotString) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  EXPECT_CALL(*vfr, audio_channel_pair_count()).WillOnce(Return(1));

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
  EXPECT_CALL(*vfr, audio_channel_pair_count()).WillOnce(Return(1));

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

  EXPECT_CALL(*vfr, audio_channel_pair_count()).WillOnce(Return(1));
  // Without a channel_pair parameter the stage defaults to channel pair 0.
  EXPECT_CALL(*deps, write_audio_wav(vfr.get(), "out.wav", 0))
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

  EXPECT_CALL(*vfr, audio_channel_pair_count()).WillOnce(Return(1));
  EXPECT_CALL(*deps, write_audio_wav(vfr.get(), "out.wav", 0))
      .WillOnce(Return(orc::AudioSinkWriteResult{false, 0, "disk full"}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.wav")}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Error: disk full");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(AudioSinkStageTest,
     Descriptor_ChannelPairDefaultsToZeroWithContainerRange) {
  orc::AudioSinkStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  auto it = std::find_if(descriptors.begin(), descriptors.end(),
                         [](const orc::ParameterDescriptor& d) {
                           return d.name == "channel_pair";
                         });

  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->type, orc::ParameterType::STRING);
  ASSERT_TRUE(it->constraints.default_value.has_value());
  EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), "0");
  // One allowed string per container channel-pair slot (0-based); the GUI
  // narrows this to the pairs the input actually carries.
  EXPECT_EQ(it->constraints.allowed_strings.size(), orc::kMaxAudioChannelPairs);
}

TEST(AudioSinkStageTest, Trigger_PassesSelectedChannelPairToDeps) {
  orc::AudioSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockAudioSinkStageDeps>>();
  stage.set_deps_override(deps);
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*vfr, audio_channel_pair_count()).WillOnce(Return(3));
  EXPECT_CALL(*deps, write_audio_wav(vfr.get(), "out.wav", 2))
      .WillOnce(Return(orc::AudioSinkWriteResult{true, 99, ""}));

  const bool result = stage.trigger({vfr},
                                    {{"output_path", std::string("out.wav")},
                                     {"channel_pair", std::string("2")}},
                                    observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Success: 99 samples written");
}

TEST(AudioSinkStageTest, Trigger_AcceptsLegacyIntegerChannelPair) {
  // Projects saved before the drop-down conversion stored channel_pair as an
  // integer; the stage still accepts that form.
  orc::AudioSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockAudioSinkStageDeps>>();
  stage.set_deps_override(deps);
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();

  EXPECT_CALL(*vfr, audio_channel_pair_count()).WillOnce(Return(3));
  EXPECT_CALL(*deps, write_audio_wav(vfr.get(), "out.wav", 2))
      .WillOnce(Return(orc::AudioSinkWriteResult{true, 99, ""}));

  const bool result = stage.trigger({vfr},
                                    {{"output_path", std::string("out.wav")},
                                     {"channel_pair", static_cast<int32_t>(2)}},
                                    observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Success: 99 samples written");
}

TEST(AudioSinkStageTest, Trigger_FailsWhenChannelPairIsOutOfRange) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  EXPECT_CALL(*vfr, audio_channel_pair_count()).WillOnce(Return(1));

  const bool result = stage.trigger({vfr},
                                    {{"output_path", std::string("out.wav")},
                                     {"channel_pair", std::string("8")}},
                                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: channel_pair parameter must be between 0 and 7");
}
}  // namespace orc_unit_test
