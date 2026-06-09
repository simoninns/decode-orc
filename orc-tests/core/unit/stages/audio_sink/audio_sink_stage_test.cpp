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
#include "../../include/video_field_representation_mock.h"

namespace orc_unit_test {
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

class MockAudioSinkStageDeps : public orc::IAudioSinkStageDeps {
 public:
  MOCK_METHOD(orc::AudioSinkWriteResult, write_audio_wav,
              (const orc::VideoFieldRepresentation* representation,
               const std::string& output_path),
              (override));
};

TEST(AudioSinkStageTest, stageInterfaceInvariantsMatchSink) {
  orc::AudioSinkStage stage;
  EXPECT_EQ(stage.required_input_count(), 1u);
  EXPECT_EQ(stage.output_count(), 0u);
  EXPECT_EQ(stage.get_node_type_info().type, orc::NodeType::SINK);
}

TEST(AudioSinkStageTest, descriptorDefaultsOutputPathIsEmptyWav) {
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

TEST(AudioSinkStageTest, triggerFailsWhenNoInputProvided) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger({}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: Audio sink requires one input (VideoFieldRepresentation)");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(AudioSinkStageTest, triggerFailsWhenInputHasNoAudio) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*vfr, has_audio()).WillOnce(Return(false));

  const bool result =
      stage.trigger({vfr}, {{"output_path", std::string("ignored.wav")}},
                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: Input VFR does not have audio data (no PCM file specified "
            "in source?)");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(AudioSinkStageTest, triggerFailsWhenInputIsNotVideoFieldRepresentation) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger({orc::ArtifactPtr{}},
                                    {{"output_path", std::string("out.wav")}},
                                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: Input must be a VideoFieldRepresentation");
}

TEST(AudioSinkStageTest, triggerFailsWhenOutputPathMissing) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
  EXPECT_CALL(*vfr, has_audio()).WillOnce(Return(true));

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: output_path parameter is required");
}

TEST(AudioSinkStageTest, triggerFailsWhenOutputPathIsNotString) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
  EXPECT_CALL(*vfr, has_audio()).WillOnce(Return(true));

  const bool result =
      stage.trigger({vfr}, {{"output_path", static_cast<int32_t>(1)}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: output_path parameter must be a string");
}

TEST(AudioSinkStageTest, triggerFailsWhenOutputPathIsEmpty) {
  orc::AudioSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
  EXPECT_CALL(*vfr, has_audio()).WillOnce(Return(true));

  const bool result = stage.trigger({vfr}, {{"output_path", std::string("")}},
                                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: output_path parameter is empty");
}

TEST(AudioSinkStageTest, triggerUsesDepsSeamAndReportsSuccess) {
  orc::AudioSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockAudioSinkStageDeps>>();
  stage.set_deps_override(deps);
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*vfr, has_audio()).WillOnce(Return(true));
  EXPECT_CALL(*deps, write_audio_wav(vfr.get(), "out.wav"))
      .WillOnce(Return(orc::AudioSinkWriteResult{true, 123, ""}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.wav")}}, observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Success: 123 samples written");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(AudioSinkStageTest, triggerUsesDepsSeamAndPropagatesFailure) {
  orc::AudioSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockAudioSinkStageDeps>>();
  stage.set_deps_override(deps);
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*vfr, has_audio()).WillOnce(Return(true));
  EXPECT_CALL(*deps, write_audio_wav(vfr.get(), "out.wav"))
      .WillOnce(Return(orc::AudioSinkWriteResult{false, 0, "disk full"}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.wav")}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Error: disk full");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}
}  // namespace orc_unit_test
