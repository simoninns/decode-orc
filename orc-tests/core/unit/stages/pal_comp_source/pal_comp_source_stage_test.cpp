/*
 * File:        pal_comp_source_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for PALCompSourceStage parameter descriptors,
 * defaults, set_parameters validation, and stage interface invariants.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/pal_comp_source/pal_comp_source_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../../orc/common/include/error_types.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../include/video_field_representation_mock.h"
#include "../source_common/source_stage_descriptor_test_utils.h"

using testing::_;  // NOLINT(bugprone-reserved-identifier)
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace orc_unit_test {
class MockPALCompSourceLoader : public orc::IPALCompSourceLoader {
 public:
  MOCK_METHOD(std::shared_ptr<orc::VideoFieldRepresentation>, load,
              (const std::string& input_path, const std::string& db_path,
               const std::string& pcm_path, const std::string& efm_path,
               const std::string& ac3rf_path),
              (const, override));
};

orc::SourceParameters make_pal_family_comp_source_parameters(
    orc::VideoSystem system) {
  orc::SourceParameters params;
  params.system = system;
  params.decoder = "ld-decode";
  params.field_width = 909;
  params.field_height = (system == orc::VideoSystem::PAL) ? 313 : 263;
  params.number_of_sequential_fields = 1449;
  return params;
}

// =========================================================================
// Parameter descriptor tests
// =========================================================================

TEST(PALCompSourceStageTest, parameterDescriptorsContainsInputPath) {
  orc::PALCompSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descriptors, "input_path", ".tbc");
}

TEST(PALCompSourceStageTest, parameterDescriptorsContainsPcmPath) {
  orc::PALCompSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descriptors, "pcm_path", ".pcm");
}

TEST(PALCompSourceStageTest, parameterDescriptorsContainsEfmPath) {
  orc::PALCompSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descriptors, "efm_path", ".efm");
}

TEST(PALCompSourceStageTest, descriptorDefaultsInputPathIsEmptyString) {
  orc::PALCompSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_empty_string_default(descriptors, "input_path");
}

TEST(PALCompSourceStageTest, descriptorDefaultsPcmPathIsEmptyString) {
  orc::PALCompSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_empty_string_default(descriptors, "pcm_path");
}

TEST(PALCompSourceStageTest, descriptorDefaultsEfmPathIsEmptyString) {
  orc::PALCompSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_empty_string_default(descriptors, "efm_path");
}

TEST(PALCompSourceStageTest, parameterDescriptorsAllParametersAreOptional) {
  orc::PALCompSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_all_descriptors_optional(descriptors);
}

// =========================================================================
// set_parameters validation tests
// =========================================================================

TEST(PALCompSourceStageTest, setParametersAcceptsValidStringMap) {
  orc::PALCompSourceStage stage;
  const std::map<std::string, orc::ParameterValue> params = {
      {"input_path", std::string("/some/file.tbc")},
      {"pcm_path", std::string("")},
      {"efm_path", std::string("")}};

  EXPECT_TRUE(stage.set_parameters(params));
}

TEST(PALCompSourceStageTest, setParametersRejectsNonStringInputPath) {
  orc::PALCompSourceStage stage;
  const std::map<std::string, orc::ParameterValue> params = {
      {"input_path", static_cast<int32_t>(42)}};

  EXPECT_FALSE(stage.set_parameters(params));
}

TEST(PALCompSourceStageTest, setParametersAcceptsEmptyMap) {
  orc::PALCompSourceStage stage;
  EXPECT_TRUE(stage.set_parameters({}));
}

// =========================================================================
// execute() contract tests
// =========================================================================

TEST(PALCompSourceStageTest, executeThrowsWhenInputProvided) {
  orc::PALCompSourceStage stage;
  orc::ObservationContext observation_context;

  EXPECT_THROW(stage.execute({nullptr}, {}, observation_context),
               std::runtime_error);
}

TEST(PALCompSourceStageTest, executeReturnsEmptyWhenInputPathMissing) {
  orc::PALCompSourceStage stage;
  orc::ObservationContext observation_context;

  const auto outputs = stage.execute({}, {}, observation_context);

  EXPECT_TRUE(outputs.empty());
}

TEST(PALCompSourceStageTest, executeReturnsEmptyWhenInputPathEmpty) {
  orc::PALCompSourceStage stage;
  orc::ObservationContext observation_context;

  const auto outputs =
      stage.execute({}, {{"input_path", std::string("")}}, observation_context);

  EXPECT_TRUE(outputs.empty());
}

TEST(PALCompSourceStageTest,
     executeLoadsPalMRepresentationThroughInjectedLoader) {
  auto loader = std::make_shared<StrictMock<MockPALCompSourceLoader>>();
  auto representation =
      std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
  orc::ObservationContext observation_context;
  orc::PALCompSourceStage stage(loader);

  EXPECT_CALL(*representation, get_video_parameters())
      .Times(1)
      .WillOnce(Return(
          make_pal_family_comp_source_parameters(orc::VideoSystem::PAL_M)));

  EXPECT_CALL(*loader,
              load("/tmp/source.tbc", "/tmp/source.tbc.db", "", "", ""))
      .Times(1)
      .WillOnce(Return(std::static_pointer_cast<orc::VideoFieldRepresentation>(
          representation)));

  const auto outputs =
      stage.execute({}, {{"input_path", std::string("/tmp/source.tbc")}},
                    observation_context);

  ASSERT_EQ(outputs.size(), 1u);
  EXPECT_EQ(outputs.front(),
            std::static_pointer_cast<orc::Artifact>(representation));
  EXPECT_TRUE(stage.supports_preview());
}

TEST(PALCompSourceStageTest, executeThrowsWhenLoadedMetadataIsNotPalFamily) {
  auto loader = std::make_shared<StrictMock<MockPALCompSourceLoader>>();
  auto representation =
      std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
  orc::ObservationContext observation_context;
  orc::PALCompSourceStage stage(loader);

  EXPECT_CALL(*representation, get_video_parameters())
      .Times(1)
      .WillOnce(Return(
          make_pal_family_comp_source_parameters(orc::VideoSystem::NTSC)));

  EXPECT_CALL(*loader, load(_, _, _, _, _))
      .Times(1)
      .WillOnce(Return(std::static_pointer_cast<orc::VideoFieldRepresentation>(
          representation)));

  EXPECT_THROW(
      stage.execute({}, {{"input_path", std::string("/tmp/source.tbc")}},
                    observation_context),
      orc::UserDataError);
}

TEST(PALCompSourceStageTest, generateReportUsesInjectedLoaderMetadataForPalM) {
  auto loader = std::make_shared<StrictMock<MockPALCompSourceLoader>>();
  auto representation =
      std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
  orc::PALCompSourceStage stage(loader);

  ASSERT_TRUE(
      stage.set_parameters({{"input_path", std::string("/tmp/source.tbc")}}));

  EXPECT_CALL(*representation, get_video_parameters())
      .Times(1)
      .WillOnce(Return(
          make_pal_family_comp_source_parameters(orc::VideoSystem::PAL_M)));
  EXPECT_CALL(*representation, field_range())
      .Times(1)
      .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(2))));
  EXPECT_CALL(*representation, get_audio_sample_count(orc::FieldID(0)))
      .Times(1)
      .WillOnce(Return(100u));
  EXPECT_CALL(*representation, get_audio_sample_count(orc::FieldID(1)))
      .Times(1)
      .WillOnce(Return(200u));
  EXPECT_CALL(*representation, get_efm_sample_count(orc::FieldID(0)))
      .Times(1)
      .WillOnce(Return(10u));
  EXPECT_CALL(*representation, get_efm_sample_count(orc::FieldID(1)))
      .Times(1)
      .WillOnce(Return(20u));
  EXPECT_CALL(*representation, has_audio()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(*representation, has_efm()).Times(1).WillOnce(Return(true));

  EXPECT_CALL(*loader,
              load("/tmp/source.tbc", "/tmp/source.tbc.db", "", "", ""))
      .Times(1)
      .WillOnce(Return(std::static_pointer_cast<orc::VideoFieldRepresentation>(
          representation)));

  const auto report = stage.generate_report();

  if (!report.has_value()) {
    FAIL() << "Expected report to have a value";
    return;
  }
  EXPECT_EQ(report->summary, "PAL Source Status");
  EXPECT_EQ(std::get<int64_t>(report->metrics.at("field_count")), 1449);
  EXPECT_EQ(std::get<int64_t>(report->metrics.at("field_width")), 909);
  EXPECT_EQ(std::get<int64_t>(report->metrics.at("field_height")), 263);
  EXPECT_EQ(std::get<int64_t>(report->metrics.at("audio_samples")), 300);
  EXPECT_EQ(std::get<int64_t>(report->metrics.at("efm_tvalues")), 30);
}

}  // namespace orc_unit_test
