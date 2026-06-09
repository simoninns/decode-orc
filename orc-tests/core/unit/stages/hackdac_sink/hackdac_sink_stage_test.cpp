/*
 * File:        hackdac_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for HackdacSinkStage parameter contracts and trigger
 * validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/hackdac_sink/hackdac_sink_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../orc/plugins/stages/hackdac_sink/hackdac_sink_stage_deps_interface.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_field_representation_mock.h"

namespace orc_unit_test {
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

class MockHackdacSinkStageDeps : public orc::IHackdacSinkStageDeps {
 public:
  MOCK_METHOD(void, init,
              (orc::TriggerProgressCallback progress_callback,
               std::atomic<bool>* cancel_requested),
              (override));
  MOCK_METHOD(orc::HackdacSinkExportResult, export_hackdac,
              (const orc::VideoFieldRepresentation* representation,
               const orc::HackdacSinkExportOptions& options),
              (override));
};

TEST(HackdacSinkStageTest, descriptorDefaultsOutputPathIsEmptyHdac) {
  orc::HackdacSinkStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  auto it = std::find_if(descriptors.begin(), descriptors.end(),
                         [](const orc::ParameterDescriptor& d) {
                           return d.name == "output_path";
                         });

  ASSERT_NE(it, descriptors.end());
  EXPECT_EQ(it->type, orc::ParameterType::FILE_PATH);
  EXPECT_EQ(it->file_extension_hint, ".hdac");
  if (!it->constraints.default_value.has_value()) {
    FAIL() << "Expected default_value to have a value";
    return;
  }
  EXPECT_EQ(std::get<std::string>(*it->constraints.default_value), "");
  EXPECT_TRUE(it->constraints.required);
}

TEST(HackdacSinkStageTest, triggerFailsWhenNoInputProvided) {
  orc::HackdacSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger(
      {}, {{"output_path", std::string("out.hdac")}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Error: No input connected");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(HackdacSinkStageTest,
     triggerFailsWhenInputIsNotVideoFieldRepresentation) {
  orc::HackdacSinkStage stage;
  MockObservationContext observation_context;

  const bool result =
      stage.trigger({nullptr}, {{"output_path", std::string("out.hdac")}},
                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: Input is not a VideoFieldRepresentation");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(HackdacSinkStageTest, triggerFailsWhenOutputPathMissing) {
  orc::HackdacSinkStage stage;
  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  const bool result = stage.trigger({vfr}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(),
            "Error: output_path parameter is required and must be a string");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(HackdacSinkStageTest, triggerUsesDepsSeamAndReportsSuccess) {
  orc::HackdacSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockHackdacSinkStageDeps>>();
  stage.set_deps_override(deps);

  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*deps, export_hackdac(vfr.get(), testing::_))
      .WillOnce(Return(orc::HackdacSinkExportResult{
          true, 12, "Success: 12 fields exported"}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.hdac")}}, observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Success: 12 fields exported");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(HackdacSinkStageTest, triggerUsesDepsSeamAndPropagatesFailure) {
  orc::HackdacSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockHackdacSinkStageDeps>>();
  stage.set_deps_override(deps);

  MockObservationContext observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*deps, export_hackdac(vfr.get(), testing::_))
      .WillOnce(
          Return(orc::HackdacSinkExportResult{false, 0, "Cancelled by user"}));

  const bool result = stage.trigger(
      {vfr}, {{"output_path", std::string("out.hdac")}}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_EQ(stage.get_trigger_status(), "Cancelled by user");
  EXPECT_FALSE(stage.is_trigger_in_progress());
}
}  // namespace orc_unit_test
