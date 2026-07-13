/*
 * File:        audio_channel_map_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for AudioChannelMapStage (channel-pair delete,
 *              in-place SMPTE 272M mono, and mono copy to a target pair)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/audio_channel_map/audio_channel_map_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <orc/stage/observation_context.h>

#include <algorithm>

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

// Two-pair source: "Analogue" pair 0 and "EFM digital audio" pair 1, each
// carrying two stereo pairs with distinct 24-bit L/R values for frame 0.
std::shared_ptr<NiceMock<MockVideoFrameRepresentationArtifact>>
make_two_pair_source() {
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(2u));
  ON_CALL(*vfr, get_audio_channel_pair_descriptor(0))
      .WillByDefault(Return(orc::AudioChannelPairDescriptor{
          "Analogue", orc::AudioOrigin::ANALOGUE}));
  ON_CALL(*vfr, get_audio_channel_pair_descriptor(1))
      .WillByDefault(Return(orc::AudioChannelPairDescriptor{
          "EFM digital audio", orc::AudioOrigin::EFM}));
  ON_CALL(*vfr, get_audio_samples(0, orc::FrameID(0)))
      .WillByDefault(
          Return(std::vector<int32_t>{100000, 200000, -300000, 400000}));
  ON_CALL(*vfr, get_audio_samples(1, orc::FrameID(0)))
      .WillByDefault(Return(std::vector<int32_t>{5000, 6000, 7000, 8000}));
  return vfr;
}

std::shared_ptr<const orc::VideoFrameRepresentation> run(
    orc::AudioChannelMapStage& stage, orc::ArtifactPtr input,
    const std::map<std::string, orc::ParameterValue>& params) {
  orc::ObservationContext ctx;
  auto outputs = stage.execute({std::move(input)}, params, ctx);
  EXPECT_EQ(outputs.size(), 1u);
  auto output = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
      outputs[0]);
  EXPECT_NE(output, nullptr);
  return output;
}

TEST(AudioChannelMapStageTest, NodeTypeInfo_ReportsTransformWithOneInput) {
  orc::AudioChannelMapStage stage;
  const auto info = stage.get_node_type_info();

  EXPECT_EQ(info.type, orc::NodeType::TRANSFORM);
  EXPECT_EQ(info.stage_name, "audio_channel_map");
  EXPECT_EQ(stage.required_input_count(), 1u);
  EXPECT_EQ(stage.output_count(), 1u);
}

TEST(AudioChannelMapStageTest, Descriptors_DefaultsRoundTripThroughSetGet) {
  orc::AudioChannelMapStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  const auto* channel_pair = find_descriptor(descriptors, "channel_pair");
  const auto* operation = find_descriptor(descriptors, "operation");
  const auto* target_pair = find_descriptor(descriptors, "target_pair");
  ASSERT_NE(channel_pair, nullptr);
  ASSERT_NE(operation, nullptr);
  ASSERT_NE(target_pair, nullptr);

  // channel_pair is a string dropdown of the possible container pair slots.
  EXPECT_EQ(channel_pair->type, orc::ParameterType::STRING);
  EXPECT_EQ(channel_pair->constraints.allowed_strings.size(),
            orc::kMaxAudioChannelPairs);
  EXPECT_EQ(std::get<std::string>(*channel_pair->constraints.default_value),
            "0");

  EXPECT_EQ(operation->type, orc::ParameterType::STRING);
  EXPECT_EQ(std::get<std::string>(*operation->constraints.default_value),
            "left_to_mono");
  EXPECT_EQ(operation->constraints.allowed_strings.size(), 5u);

  // target_pair offers "new" plus the slots, and is shown only for the copy
  // ops.
  EXPECT_EQ(target_pair->type, orc::ParameterType::STRING);
  EXPECT_EQ(std::get<std::string>(*target_pair->constraints.default_value),
            "new");
  EXPECT_EQ(target_pair->constraints.allowed_strings.size(),
            orc::kMaxAudioChannelPairs + 1);
  ASSERT_TRUE(target_pair->constraints.depends_on.has_value());
  EXPECT_EQ(target_pair->constraints.depends_on->parameter_name, "operation");
  EXPECT_THAT(
      target_pair->constraints.depends_on->required_values,
      ::testing::ElementsAre("copy_left_to_target", "copy_right_to_target"));

  // set_description is a bool gate hidden only for delete; description is
  // free-form and shown only when set_description is true.
  const auto* set_description = find_descriptor(descriptors, "set_description");
  ASSERT_NE(set_description, nullptr);
  EXPECT_EQ(set_description->type, orc::ParameterType::BOOL);
  EXPECT_FALSE(std::get<bool>(*set_description->constraints.default_value));
  ASSERT_TRUE(set_description->constraints.depends_on.has_value());
  EXPECT_THAT(
      set_description->constraints.depends_on->required_values,
      ::testing::ElementsAre("left_to_mono", "right_to_mono",
                             "copy_left_to_target", "copy_right_to_target"));

  const auto* description = find_descriptor(descriptors, "description");
  ASSERT_NE(description, nullptr);
  EXPECT_EQ(description->type, orc::ParameterType::STRING);
  EXPECT_TRUE(description->constraints.allowed_strings.empty());
  ASSERT_TRUE(description->constraints.depends_on.has_value());
  EXPECT_EQ(description->constraints.depends_on->parameter_name,
            "set_description");
  EXPECT_THAT(description->constraints.depends_on->required_values,
              ::testing::ElementsAre("true"));

  auto params = stage.get_parameters();
  EXPECT_EQ(std::get<std::string>(params.at("channel_pair")), "0");
  EXPECT_EQ(std::get<std::string>(params.at("operation")), "left_to_mono");
  EXPECT_EQ(std::get<std::string>(params.at("target_pair")), "new");
  EXPECT_FALSE(std::get<bool>(params.at("set_description")));
  EXPECT_EQ(std::get<std::string>(params.at("description")), "");

  EXPECT_TRUE(
      stage.set_parameters({{"channel_pair", std::string("3")},
                            {"operation", std::string("copy_right_to_target")},
                            {"target_pair", std::string("2")},
                            {"set_description", true},
                            {"description", std::string("French language")}}));
  params = stage.get_parameters();
  EXPECT_EQ(std::get<std::string>(params.at("channel_pair")), "3");
  EXPECT_EQ(std::get<std::string>(params.at("operation")),
            "copy_right_to_target");
  EXPECT_EQ(std::get<std::string>(params.at("target_pair")), "2");
  EXPECT_TRUE(std::get<bool>(params.at("set_description")));
  EXPECT_EQ(std::get<std::string>(params.at("description")), "French language");
}

TEST(AudioChannelMapStageTest, Description_NamesTheResultPairWhenEnabled) {
  // The bilingual workflow: copy the right channel to a new pair named "French
  // language", then rename the source's left-mono to "English language".
  {
    orc::AudioChannelMapStage stage;
    auto vfr = make_two_pair_source();
    auto output = run(stage, vfr,
                      {{"channel_pair", std::string("0")},
                       {"operation", std::string("copy_right_to_target")},
                       {"target_pair", std::string("new")},
                       {"set_description", true},
                       {"description", std::string("French language")}});
    ASSERT_EQ(output->audio_channel_pair_count(), 3u);
    const auto appended = output->get_audio_channel_pair_descriptor(2);
    ASSERT_TRUE(appended.has_value());
    EXPECT_EQ(appended->name, "French language");
    EXPECT_EQ(appended->origin, orc::AudioOrigin::DERIVED);
    EXPECT_EQ(output->get_audio_samples(2, orc::FrameID(0)),
              (std::vector<int32_t>{200000, 0, 400000, 0}));
  }
  {
    orc::AudioChannelMapStage stage;
    auto vfr = make_two_pair_source();
    auto output = run(stage, vfr,
                      {{"channel_pair", std::string("0")},
                       {"operation", std::string("left_to_mono")},
                       {"set_description", true},
                       {"description", std::string("English language")}});
    const auto desc = output->get_audio_channel_pair_descriptor(0);
    ASSERT_TRUE(desc.has_value());
    EXPECT_EQ(desc->name, "English language");
    EXPECT_EQ(desc->origin, orc::AudioOrigin::DERIVED);
  }
}

TEST(AudioChannelMapStageTest, Description_KeepsExistingNameWhenDisabled) {
  // With 'Add description' off, the result pair keeps the source's existing
  // name (no override, no derived suffix).
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  auto output = run(stage, vfr,
                    {{"channel_pair", std::string("0")},
                     {"operation", std::string("left_to_mono")},
                     {"set_description", false},
                     {"description", std::string("ignored")}});
  const auto desc = output->get_audio_channel_pair_descriptor(0);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->name, "Analogue");
  EXPECT_EQ(desc->origin, orc::AudioOrigin::DERIVED);
}

TEST(AudioChannelMapStageTest, SetParameters_RejectsInvalidValues) {
  orc::AudioChannelMapStage stage;
  EXPECT_FALSE(stage.set_parameters({{"channel_pair", std::string("bogus")}}));
  EXPECT_FALSE(stage.set_parameters({{"channel_pair", std::string("-1")}}));
  EXPECT_FALSE(stage.set_parameters(
      {{"channel_pair",
        std::to_string(static_cast<int>(orc::kMaxAudioChannelPairs))}}));
  EXPECT_FALSE(stage.set_parameters({{"channel_pair", int32_t{1}}}));
  EXPECT_FALSE(
      stage.set_parameters({{"operation", std::string("swap_channels")}}));
  EXPECT_FALSE(stage.set_parameters({{"target_pair", std::string("bogus")}}));
  // "new" and valid indices are accepted.
  EXPECT_TRUE(stage.set_parameters({{"target_pair", std::string("new")}}));
  EXPECT_TRUE(stage.set_parameters({{"target_pair", std::string("1")}}));
}

TEST(AudioChannelMapStageTest, Execute_ThrowsOnMissingInput) {
  orc::AudioChannelMapStage stage;
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({}, {}, ctx), orc::DAGExecutionError);
}

TEST(AudioChannelMapStageTest, Execute_ThrowsWhenSourcePairOutOfRange) {
  orc::AudioChannelMapStage stage;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count()).WillByDefault(Return(1u));

  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({vfr}, {{"channel_pair", std::string("1")}}, ctx),
               orc::DAGExecutionError);
}

TEST(AudioChannelMapStageTest, LeftToMono_KeepsLeftAndSilencesRight) {
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  auto output = run(stage, vfr,
                    {{"channel_pair", std::string("0")},
                     {"operation", std::string("left_to_mono")}});

  EXPECT_EQ(output->audio_channel_pair_count(), 2u);
  const auto desc = output->get_audio_channel_pair_descriptor(0);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->name, "Analogue");
  EXPECT_EQ(desc->origin, orc::AudioOrigin::DERIVED);

  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)),
            (std::vector<int32_t>{100000, 0, -300000, 0}));
  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(0)),
            (std::vector<int32_t>{5000, 6000, 7000, 8000}));
}

TEST(AudioChannelMapStageTest, RightToMono_MovesRightToLeftAndSilencesRight) {
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  auto output = run(stage, vfr,
                    {{"channel_pair", std::string("0")},
                     {"operation", std::string("right_to_mono")}});

  EXPECT_EQ(output->audio_channel_pair_count(), 2u);
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)),
            (std::vector<int32_t>{200000, 0, 400000, 0}));
  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(0)),
            (std::vector<int32_t>{5000, 6000, 7000, 8000}));
}

TEST(AudioChannelMapStageTest, CopyLeftToNewTarget_AppendsMonoAndKeepsSource) {
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  auto output = run(stage, vfr,
                    {{"channel_pair", std::string("0")},
                     {"operation", std::string("copy_left_to_target")},
                     {"target_pair", std::string("new")}});

  // A new pair is appended; source and other pairs are untouched.
  ASSERT_EQ(output->audio_channel_pair_count(), 3u);
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)),
            (std::vector<int32_t>{100000, 200000, -300000, 400000}));
  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(0)),
            (std::vector<int32_t>{5000, 6000, 7000, 8000}));
  EXPECT_EQ(output->get_audio_samples(2, orc::FrameID(0)),
            (std::vector<int32_t>{100000, 0, -300000, 0}));

  const auto appended = output->get_audio_channel_pair_descriptor(2);
  ASSERT_TRUE(appended.has_value());
  EXPECT_EQ(appended->name, "Analogue");
  EXPECT_EQ(appended->origin, orc::AudioOrigin::DERIVED);
  // Source descriptor unchanged.
  EXPECT_EQ(output->get_audio_channel_pair_descriptor(0)->name, "Analogue");
}

TEST(AudioChannelMapStageTest, CopyRightToNewTarget_AppendsRightChannelMono) {
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  auto output = run(stage, vfr,
                    {{"channel_pair", std::string("0")},
                     {"operation", std::string("copy_right_to_target")},
                     {"target_pair", std::string("new")}});

  ASSERT_EQ(output->audio_channel_pair_count(), 3u);
  EXPECT_EQ(output->get_audio_samples(2, orc::FrameID(0)),
            (std::vector<int32_t>{200000, 0, 400000, 0}));
}

TEST(AudioChannelMapStageTest, CopyLeftToExistingTarget_OverwritesTargetOnly) {
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  auto output = run(stage, vfr,
                    {{"channel_pair", std::string("0")},
                     {"operation", std::string("copy_left_to_target")},
                     {"target_pair", std::string("1")}});

  // No pair added; pair 1 becomes the source's left mono, source untouched.
  ASSERT_EQ(output->audio_channel_pair_count(), 2u);
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)),
            (std::vector<int32_t>{100000, 200000, -300000, 400000}));
  EXPECT_EQ(output->get_audio_samples(1, orc::FrameID(0)),
            (std::vector<int32_t>{100000, 0, -300000, 0}));

  const auto target = output->get_audio_channel_pair_descriptor(1);
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(target->name, "Analogue");
  EXPECT_EQ(target->origin, orc::AudioOrigin::DERIVED);
}

TEST(AudioChannelMapStageTest, CopyToTarget_ThrowsWhenTargetOutOfRange) {
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({vfr},
                             {{"channel_pair", std::string("0")},
                              {"operation", std::string("copy_left_to_target")},
                              {"target_pair", std::string("5")}},
                             ctx),
               orc::DAGExecutionError);
}

TEST(AudioChannelMapStageTest, CopyToNewTarget_ThrowsWhenPairCapExceeded) {
  orc::AudioChannelMapStage stage;
  auto vfr = std::make_shared<NiceMock<MockVideoFrameRepresentationArtifact>>();
  ON_CALL(*vfr, audio_channel_pair_count())
      .WillByDefault(Return(orc::kMaxAudioChannelPairs));

  orc::ObservationContext ctx;
  EXPECT_THROW(stage.execute({vfr},
                             {{"channel_pair", std::string("0")},
                              {"operation", std::string("copy_left_to_target")},
                              {"target_pair", std::string("new")}},
                             ctx),
               orc::DAGExecutionError);
}

TEST(AudioChannelMapStageTest, Delete_RemovesTargetAndShiftsLaterPairsDown) {
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  auto output = run(stage, vfr,
                    {{"channel_pair", std::string("0")},
                     {"operation", std::string("delete")}});

  ASSERT_EQ(output->audio_channel_pair_count(), 1u);
  const auto desc = output->get_audio_channel_pair_descriptor(0);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->name, "EFM digital audio");
  EXPECT_EQ(desc->origin, orc::AudioOrigin::EFM);
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)),
            (std::vector<int32_t>{5000, 6000, 7000, 8000}));

  EXPECT_TRUE(output->get_audio_samples(1, orc::FrameID(0)).empty());
  EXPECT_FALSE(output->get_audio_channel_pair_descriptor(1).has_value());
}

TEST(AudioChannelMapStageTest, Delete_OfLaterPairKeepsEarlierPairIntact) {
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  auto output = run(stage, vfr,
                    {{"channel_pair", std::string("1")},
                     {"operation", std::string("delete")}});

  ASSERT_EQ(output->audio_channel_pair_count(), 1u);
  EXPECT_EQ(output->get_audio_channel_pair_descriptor(0)->name, "Analogue");
  EXPECT_EQ(output->get_audio_samples(0, orc::FrameID(0)),
            (std::vector<int32_t>{100000, 200000, -300000, 400000}));
}

TEST(AudioChannelMapStageTest, OutOfRangePair_ReturnsEmptyAndNullopt) {
  orc::AudioChannelMapStage stage;
  auto vfr = make_two_pair_source();
  auto output = run(stage, vfr,
                    {{"channel_pair", std::string("0")},
                     {"operation", std::string("left_to_mono")}});

  EXPECT_TRUE(output->get_audio_samples(2, orc::FrameID(0)).empty());
  EXPECT_FALSE(output->get_audio_channel_pair_descriptor(2).has_value());
}

}  // namespace
}  // namespace orc_unit_test
