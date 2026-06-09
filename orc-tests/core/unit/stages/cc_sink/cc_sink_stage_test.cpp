/*
 * File:        cc_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for CCSinkStage parameter contracts and trigger
 * dependency seam
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/cc_sink/cc_sink_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>

#include "../../../../orc/plugins/stages/cc_sink/cc_sink_stage_deps_interface.h"
#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_field_representation_mock.h"

namespace orc_unit_test {
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

class MockCCSinkStageDeps : public orc::ICCSinkStageDeps {
 public:
  MOCK_METHOD(void, init,
              (orc::TriggerProgressCallback progress_callback,
               std::atomic<bool>* cancel_requested),
              (override));

  MOCK_METHOD(orc::CCExportResult, export_cc,
              (orc::VideoFieldRepresentation * representation,
               orc::IObservationContext& observation_context,
               orc::CCExportOptions options),
              (override));
};

TEST(CCSinkStageTest, descriptorDefaultsIncludeExportFormatOptions) {
  orc::CCSinkStage stage;
  const auto descriptors = stage.get_parameter_descriptors();

  auto output_it = std::find_if(descriptors.begin(), descriptors.end(),
                                [](const orc::ParameterDescriptor& d) {
                                  return d.name == "output_path";
                                });
  auto format_it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [](const orc::ParameterDescriptor& d) { return d.name == "format"; });

  ASSERT_NE(output_it, descriptors.end());
  EXPECT_EQ(output_it->type, orc::ParameterType::FILE_PATH);
  if (!output_it->constraints.default_value.has_value()) {
    FAIL() << "Expected output default_value to have a value";
    return;
  }
  EXPECT_EQ(std::get<std::string>(*output_it->constraints.default_value), "");

  ASSERT_NE(format_it, descriptors.end());
  EXPECT_EQ(format_it->type, orc::ParameterType::STRING);
  EXPECT_EQ(format_it->constraints.allowed_strings.size(), 2u);
  if (!format_it->constraints.default_value.has_value()) {
    FAIL() << "Expected format default_value to have a value";
    return;
  }
  EXPECT_EQ(std::get<std::string>(*format_it->constraints.default_value),
            "Scenarist SCC");
}

TEST(CCSinkStageTest, triggerStatusIsIdleWhenNotProcessing) {
  orc::CCSinkStage stage;
  EXPECT_EQ(stage.get_trigger_status(), "Idle");
}

TEST(CCSinkStageTest, triggerFailsWhenNoInputProvided) {
  orc::CCSinkStage stage;
  MockObservationContext observation_context;

  const bool result = stage.trigger({}, {}, observation_context);

  EXPECT_FALSE(result);
  EXPECT_FALSE(stage.is_trigger_in_progress());
  EXPECT_EQ(stage.get_trigger_status(), "Idle");
}

TEST(CCSinkStageTest, triggerFailsWhenInputIsNotVideoFieldRepresentation) {
  orc::CCSinkStage stage;
  MockObservationContext observation_context;

  const bool result =
      stage.trigger({nullptr}, {{"output_path", std::string("out.scc")}},
                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_FALSE(stage.is_trigger_in_progress());
  EXPECT_EQ(stage.get_trigger_status(), "Idle");
}

TEST(CCSinkStageTest, triggerUsesDepsSeamAndReportsSuccess) {
  orc::CCSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockCCSinkStageDeps>>();
  stage.set_deps_override(deps);

  NiceMock<MockObservationContext> observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  orc::CCExportOptions captured_options;

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps, export_cc(vfr.get(), _, _))
      .WillOnce(testing::DoAll(testing::SaveArg<2>(&captured_options),
                               Return(orc::CCExportResult{true, "ok", 42})));

  const bool result =
      stage.trigger({vfr},
                    {{"output_path", std::string("captions.scc")},
                     {"format", std::string("Scenarist SCC")}},
                    observation_context);

  EXPECT_TRUE(result);
  EXPECT_EQ(captured_options.output_path, "captions.scc");
  EXPECT_EQ(captured_options.export_format, orc::CCExportFormat::SCC);
  EXPECT_FALSE(captured_options.write_csv);
  EXPECT_FALSE(stage.is_trigger_in_progress());
  EXPECT_EQ(stage.get_trigger_status(), "Idle");
}

TEST(CCSinkStageTest, triggerUsesDepsSeamAndPropagatesFailure) {
  orc::CCSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockCCSinkStageDeps>>();
  stage.set_deps_override(deps);

  NiceMock<MockObservationContext> observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps, export_cc(vfr.get(), _, _))
      .WillOnce(Return(orc::CCExportResult{false, "export failed", 0}));

  const bool result =
      stage.trigger({vfr},
                    {{"output_path", std::string("captions.scc")},
                     {"format", std::string("Scenarist SCC")}},
                    observation_context);

  EXPECT_FALSE(result);
  EXPECT_FALSE(stage.is_trigger_in_progress());
  EXPECT_EQ(stage.get_trigger_status(), "Idle");
}

TEST(CCSinkStageTest, triggerUsesDepsSeamWithSCCFormat) {
  orc::CCSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockCCSinkStageDeps>>();
  stage.set_deps_override(deps);

  NiceMock<MockObservationContext> observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps,
              export_cc(vfr.get(), _,
                        testing::Field(&orc::CCExportOptions::export_format,
                                       orc::CCExportFormat::SCC)))
      .WillOnce(Return(orc::CCExportResult{true, "ok", 1}));

  const bool result =
      stage.trigger({vfr},
                    {{"output_path", std::string("captions.scc")},
                     {"format", std::string("Scenarist SCC")}},
                    observation_context);

  EXPECT_TRUE(result);
  EXPECT_FALSE(stage.is_trigger_in_progress());
}

TEST(CCSinkStageTest, triggerUsesDepsSeamWithPlainTextFormat) {
  orc::CCSinkStage stage;
  auto deps = std::make_shared<StrictMock<MockCCSinkStageDeps>>();
  stage.set_deps_override(deps);

  NiceMock<MockObservationContext> observation_context;
  auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

  EXPECT_CALL(*deps, init(_, _));
  EXPECT_CALL(*deps,
              export_cc(vfr.get(), _,
                        testing::Field(&orc::CCExportOptions::export_format,
                                       orc::CCExportFormat::PLAIN_TEXT)))
      .WillOnce(Return(orc::CCExportResult{true, "ok", 1}));

  const bool result =
      stage.trigger({vfr},
                    {{"output_path", std::string("captions.txt")},
                     {"format", std::string("Plain Text")}},
                    observation_context);

  EXPECT_TRUE(result);
  EXPECT_FALSE(stage.is_trigger_in_progress());
}
}  // namespace orc_unit_test
