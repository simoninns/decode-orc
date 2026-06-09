/*
 * File:        ntsc_yc_source_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for NTSCYCSourceStage parameter descriptors,
 * defaults, set_parameters validation, and stage interface invariants.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/ntsc_yc_source/ntsc_yc_source_stage.h"

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
class MockNTSCYCSourceLoader : public orc::INTSCYCSourceLoader {
 public:
  MOCK_METHOD(std::shared_ptr<orc::VideoFieldRepresentation>, load,
              (const std::string& y_path, const std::string& c_path,
               const std::string& db_path, const std::string& pcm_path,
               const std::string& efm_path, const std::string& ac3rf_path),
              (const, override));
};

orc::SourceParameters make_ntsc_yc_source_parameters(orc::VideoSystem system) {
  orc::SourceParameters params;
  params.system = system;
  params.decoder = "ld-decode";
  params.field_width = 910;
  params.field_height = 263;
  params.number_of_sequential_fields = 1200;
  return params;
}

// =========================================================================
// Parameter descriptor tests
// =========================================================================

TEST(NTSCYCSourceStageTest, parameterDescriptorsContainsYPath) {
  orc::NTSCYCSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descriptors, "y_path", ".tbcy");
}

TEST(NTSCYCSourceStageTest, parameterDescriptorsContainsCPath) {
  orc::NTSCYCSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descriptors, "c_path", ".tbcc");
}

TEST(NTSCYCSourceStageTest, parameterDescriptorsContainsDbPath) {
  orc::NTSCYCSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descriptors, "db_path", ".db");
}

TEST(NTSCYCSourceStageTest, parameterDescriptorsContainsPcmPath) {
  orc::NTSCYCSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descriptors, "pcm_path", ".pcm");
}

TEST(NTSCYCSourceStageTest, parameterDescriptorsContainsEfmPath) {
  orc::NTSCYCSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descriptors, "efm_path", ".efm");
}

TEST(NTSCYCSourceStageTest, descriptorDefaultsAllPathsAreEmptyString) {
  orc::NTSCYCSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();

  for (const auto& desc : descriptors) {
    if (!desc.constraints.default_value.has_value()) {
      FAIL() << "Descriptor '" << desc.name << "' has no default_value";
      return;
    }
    ASSERT_TRUE(
        std::holds_alternative<std::string>(*desc.constraints.default_value))
        << "Descriptor '" << desc.name << "' default is not a string";
    EXPECT_EQ(std::get<std::string>(*desc.constraints.default_value), "")
        << "Descriptor '" << desc.name << "' default is not empty string";
  }
}

TEST(NTSCYCSourceStageTest, parameterDescriptorsAllParametersAreOptional) {
  orc::NTSCYCSourceStage stage;
  auto descriptors = stage.get_parameter_descriptors();
  expect_all_descriptors_optional(descriptors);
}

// =========================================================================
// get_parameters / parity tests
// =========================================================================

TEST(NTSCYCSourceStageTest, getParametersDefaultYPathIsEmptyString) {
  orc::NTSCYCSourceStage stage;
  auto params = stage.get_parameters();

  auto it = params.find("y_path");
  ASSERT_NE(it, params.end());
  ASSERT_TRUE(std::holds_alternative<std::string>(it->second));
  EXPECT_EQ(std::get<std::string>(it->second), "");
}

TEST(NTSCYCSourceStageTest, getParametersDefaultCPathIsEmptyString) {
  orc::NTSCYCSourceStage stage;
  auto params = stage.get_parameters();

  auto it = params.find("c_path");
  ASSERT_NE(it, params.end());
  ASSERT_TRUE(std::holds_alternative<std::string>(it->second));
  EXPECT_EQ(std::get<std::string>(it->second), "");
}

TEST(NTSCYCSourceStageTest, getParametersDefaultDbPathIsEmptyString) {
  orc::NTSCYCSourceStage stage;
  auto params = stage.get_parameters();

  auto it = params.find("db_path");
  ASSERT_NE(it, params.end());
  ASSERT_TRUE(std::holds_alternative<std::string>(it->second));
  EXPECT_EQ(std::get<std::string>(it->second), "");
}

// =========================================================================
// set_parameters validation tests
// =========================================================================

TEST(NTSCYCSourceStageTest, setParametersAcceptsValidStringMap) {
  orc::NTSCYCSourceStage stage;
  const std::map<std::string, orc::ParameterValue> params = {
      {"y_path", std::string("/some/file.tbcy")},
      {"c_path", std::string("/some/file.tbcc")},
      {"db_path", std::string("")},
      {"pcm_path", std::string("")},
      {"efm_path", std::string("")}};

  EXPECT_TRUE(stage.set_parameters(params));
}

TEST(NTSCYCSourceStageTest, setParametersRejectsNonStringYPath) {
  orc::NTSCYCSourceStage stage;
  const std::map<std::string, orc::ParameterValue> params = {
      {"y_path", static_cast<int32_t>(99)}};

  EXPECT_FALSE(stage.set_parameters(params));
}

TEST(NTSCYCSourceStageTest, setParametersRejectsNonStringCPath) {
  orc::NTSCYCSourceStage stage;
  const std::map<std::string, orc::ParameterValue> params = {
      {"c_path", static_cast<int32_t>(99)}};

  EXPECT_FALSE(stage.set_parameters(params));
}

TEST(NTSCYCSourceStageTest, setParametersPersistsValues) {
  orc::NTSCYCSourceStage stage;
  const std::map<std::string, orc::ParameterValue> params = {
      {"y_path", std::string("/luma.tbcy")},
      {"c_path", std::string("/chroma.tbcc")}};

  ASSERT_TRUE(stage.set_parameters(params));

  auto persisted = stage.get_parameters();
  EXPECT_EQ(std::get<std::string>(persisted.at("y_path")), "/luma.tbcy");
  EXPECT_EQ(std::get<std::string>(persisted.at("c_path")), "/chroma.tbcc");
}

TEST(NTSCYCSourceStageTest, setParametersAcceptsEmptyMap) {
  orc::NTSCYCSourceStage stage;
  EXPECT_TRUE(stage.set_parameters({}));
}

// =========================================================================
// execute() contract tests
// =========================================================================

TEST(NTSCYCSourceStageTest, executeThrowsWhenInputProvided) {
  orc::NTSCYCSourceStage stage;
  orc::ObservationContext observation_context;

  EXPECT_THROW(stage.execute({nullptr}, {}, observation_context),
               std::runtime_error);
}

TEST(NTSCYCSourceStageTest, executeReturnsEmptyWhenYPathMissing) {
  orc::NTSCYCSourceStage stage;
  orc::ObservationContext observation_context;

  const auto outputs = stage.execute({}, {{"c_path", std::string("c.tbcc")}},
                                     observation_context);

  EXPECT_TRUE(outputs.empty());
}

TEST(NTSCYCSourceStageTest, executeReturnsEmptyWhenCPathMissing) {
  orc::NTSCYCSourceStage stage;
  orc::ObservationContext observation_context;

  const auto outputs = stage.execute({}, {{"y_path", std::string("y.tbcy")}},
                                     observation_context);

  EXPECT_TRUE(outputs.empty());
}

TEST(NTSCYCSourceStageTest,
     executeLoadsNtscRepresentationThroughInjectedLoader) {
  auto loader = std::make_shared<StrictMock<MockNTSCYCSourceLoader>>();
  auto representation =
      std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
  orc::ObservationContext observation_context;
  orc::NTSCYCSourceStage stage(loader);

  EXPECT_CALL(*representation, get_video_parameters())
      .Times(1)
      .WillOnce(Return(make_ntsc_yc_source_parameters(orc::VideoSystem::NTSC)));

  EXPECT_CALL(*loader, load("/tmp/source.tbcy", "/tmp/source.tbcc",
                            "/tmp/source.tbcy.db", "", "", ""))
      .Times(1)
      .WillOnce(Return(std::static_pointer_cast<orc::VideoFieldRepresentation>(
          representation)));

  const auto outputs =
      stage.execute({},
                    {{"y_path", std::string("/tmp/source.tbcy")},
                     {"c_path", std::string("/tmp/source.tbcc")}},
                    observation_context);

  ASSERT_EQ(outputs.size(), 1u);
  EXPECT_EQ(outputs.front(),
            std::static_pointer_cast<orc::Artifact>(representation));
  EXPECT_TRUE(stage.supports_preview());
}

TEST(NTSCYCSourceStageTest, executeThrowsWhenLoadedMetadataIsNotNtsc) {
  auto loader = std::make_shared<StrictMock<MockNTSCYCSourceLoader>>();
  auto representation =
      std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
  orc::ObservationContext observation_context;
  orc::NTSCYCSourceStage stage(loader);

  EXPECT_CALL(*representation, get_video_parameters())
      .Times(1)
      .WillOnce(Return(make_ntsc_yc_source_parameters(orc::VideoSystem::PAL)));

  EXPECT_CALL(*loader, load(_, _, _, _, _, _))
      .Times(1)
      .WillOnce(Return(std::static_pointer_cast<orc::VideoFieldRepresentation>(
          representation)));

  EXPECT_THROW(stage.execute({},
                             {{"y_path", std::string("/tmp/source.tbcy")},
                              {"c_path", std::string("/tmp/source.tbcc")}},
                             observation_context),
               orc::UserDataError);
}

}  // namespace orc_unit_test
