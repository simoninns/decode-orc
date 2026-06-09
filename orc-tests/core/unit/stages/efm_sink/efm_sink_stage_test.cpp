/*
 * File:        efm_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for EFMSinkStage parameter contracts and trigger
 * validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/efm_sink/efm_sink_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../orc/plugins/stages/efm_sink/efm_sink_stage_deps_interface.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_field_representation_mock.h"

namespace orc_unit_test {
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

class MockEFMSinkStageDeps : public orc::IEFMSinkStageDeps {
 public:
  MOCK_METHOD(void, init,
              (orc::TriggerProgressCallback progress_callback,
               std::atomic<bool>* cancel_requested),
              (override));
  MOCK_METHOD(orc::EFMSinkDecodeResult, decode_efm,
              (const orc::VideoFieldRepresentation* representation,
               const orc::EFMSinkOptions& options),
              (override));
};

TEST(EFMSinkStageTest, descriptorDefaultsIncludeOutputPathAndDecodeMode) {
  orc::EFMSinkStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  auto output_it = std::find_if(descriptors.begin(), descriptors.end(),
                                [](const orc::ParameterDescriptor& d) {
                                  return d.name == "output_path";
                                });
  auto mode_it = std::find_if(descriptors.begin(), descriptors.end(),
                              [](const orc::ParameterDescriptor& d) {
                                return d.name == "decode_mode";
                              });

  ASSERT_NE(output_it, descriptors.end());
  EXPECT_EQ(output_it->type, orc::ParameterType::FILE_PATH);
  EXPECT_EQ(output_it->file_extension_hint, ".wav");
  if (!output_it->constraints.default_value.has_value() ||
      !mode_it->constraints.default_value.has_value()) {
    FAIL() << "Expected all descriptors to have default values";
    return;
  }
  EXPECT_EQ(std::get<std::string>(*output_it->constraints.default_value), "");

  ASSERT_NE(mode_it, descriptors.end());
  EXPECT_EQ(mode_it->type, orc::ParameterType::STRING);
  EXPECT_EQ(std::get<std::string>(*mode_it->constraints.default_value),
            "audio");
  EXPECT_EQ(mode_it->constraints.allowed_strings.size(), 2u);
}

TEST(EFMSinkStageTest, triggerFailsWhenNoInputProvided) {
  orc::EFMSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger({}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: EFMSink requires one input (VideoFieldRepresentation)");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(EFMSinkStageTest, triggerFailsWhenInputHasNoEfm) {
  orc::EFMSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*vfr, has_efm()).WillOnce(Return(false));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.bin")}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(
      stage.get_trigger_status(),
      "Error: EFMSink: input VFR has no EFM data (no EFM file in source?)");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(EFMSinkStageTest, triggerFailsWhenOutputPathMissing) {
  orc::EFMSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*vfr, has_efm()).WillOnce(Return(true));

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: EFMSink: output_path parameter is required");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(EFMSinkStageTest, triggerUsesDepsSeamAndReportsSuccess) {
  orc::EFMSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMSinkStageDeps>>();
  stage.set_deps_override(deps);

  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*vfr, has_efm()).WillOnce(Return(true));
  EXPECT_CALL(*deps, decode_efm(vfr.get(), testing::_))
      .WillOnce(Return(
          orc::EFMSinkDecodeResult{true, "Success: EFM decoded to out.bin"}));

  const bool result = stage.trigger({vfr},
                                    {{"output_path", std::string("out.bin")},
                                     {"decode_mode", std::string("data")}},
                                    observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Success: EFM decoded to out.bin");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(EFMSinkStageTest, triggerUsesDepsSeamAndPropagatesFailure) {
  orc::EFMSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockEFMSinkStageDeps>>();
  stage.set_deps_override(deps);

  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*vfr, has_efm()).WillOnce(Return(true));
  EXPECT_CALL(*deps, decode_efm(vfr.get(), testing::_))
      .WillOnce(Return(orc::EFMSinkDecodeResult{false, "Cancelled by user"}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.bin")}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Cancelled by user");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}
}  // namespace orc_unit_test
