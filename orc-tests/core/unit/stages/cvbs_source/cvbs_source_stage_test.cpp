/*
 * File:        cvbs_source_stage_test.cpp
 * Module:      orc-tests/core/unit/stages/cvbs_source
 * Purpose:     Unit tests for the PAL and NTSC CVBS source stages
 *
 * Tests stage contract, validation, decode behavior, preview/report support,
 * and PAL/NTSC concrete stage selection.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../../../../orc/plugins/stages/cvbs_source/cvbs_source_stage.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>

#include "../../../../orc/common/include/common_types.h"
#include "../../../../orc/common/include/error_types.h"
#include "../../../../orc/common/include/parameter_types.h"
#include "../../../../orc/core/include/observation_context.h"

namespace orc {
namespace tests {

namespace {

class FakeCVBSSourceStageDeps final : public ICVBSSourceStageDeps {
 public:
  bool input_file_valid = true;
  std::string input_file_error = "Input file error";

  bool metadata_available = true;
  std::string metadata_error = "Metadata read error";
  CVBSMetadataRecord metadata_record{"PAL", "CVBS_U16_4FSC",
                                     "STANDARD_TBC_LOCKED", "composite"};
  mutable std::string last_metadata_path;

  bool payload_available = true;
  std::string payload_error = "Payload read error";
  std::vector<uint16_t> payload_words;

  FakeCVBSSourceStageDeps() {
    // One full NTSC frame at blanking level in CVBS_U16_4FSC format.
    payload_words.assign(static_cast<size_t>(910 * 525),
                         static_cast<uint16_t>(240 * 64));
  }

  bool validate_input_file(const std::string& input_path,
                           std::string& error_message) const override {
    (void)input_path;
    if (!input_file_valid) {
      error_message = input_file_error;
      return false;
    }
    return true;
  }

  std::optional<CVBSMetadataRecord> load_metadata(
      const std::string& meta_path, std::string& error_message) const override {
    last_metadata_path = meta_path;
    if (!metadata_available) {
      error_message = metadata_error;
      return std::nullopt;
    }
    return metadata_record;
  }

  std::optional<size_t> get_input_word_count(
      const std::string& input_path,
      std::string& error_message) const override {
    (void)input_path;
    if (!payload_available) {
      error_message = payload_error;
      return std::nullopt;
    }
    return payload_words.size();
  }

  bool read_input_words_at(const std::string& input_path, size_t word_offset,
                           size_t word_count, std::vector<uint16_t>& out_words,
                           std::string& error_message) const override {
    (void)input_path;
    if (!payload_available) {
      error_message = payload_error;
      return false;
    }
    if (word_offset + word_count > payload_words.size()) {
      error_message = "Read out of bounds in fake deps";
      return false;
    }
    out_words.assign(
        payload_words.begin() + static_cast<std::ptrdiff_t>(word_offset),
        payload_words.begin() +
            static_cast<std::ptrdiff_t>(word_offset + word_count));
    return true;
  }
};

void expect_user_data_error_contains(
    DAGStage& stage, const std::map<std::string, ParameterValue>& parameters,
    const std::string& expected_substring) {
  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  try {
    (void)stage.execute(inputs, parameters, observation_context);
    FAIL() << "Expected UserDataError containing: " << expected_substring;
  } catch (const UserDataError& error) {
    EXPECT_NE(std::string(error.what()).find(expected_substring),
              std::string::npos)
        << "Actual error: " << error.what();
  }
}

}  // namespace

/**
 * @class CVBSSourceContractTest
 * @brief Tests contract and behavior for CVBS source stages
 *
 * Verifies:
 * - Stage identity and node type information
 * - Parameter descriptor structure and validation constraints
 * - Parameter getter/setter mechanism
 * - Manual mode parameter validation
 * - Metadata mode path validation framework
 * - Output contract (no implementation yet)
 */
class CVBSSourceContractTest : public ::testing::Test {
 protected:
  CVBSSourceContractTest() = default;

  std::shared_ptr<PALCVBSSourceStage> stage_ =
      std::make_shared<PALCVBSSourceStage>();
};

// ====================
// Stage Identity Tests
// ====================

TEST_F(CVBSSourceContractTest, StageIdentity_HasCorrectNodeType) {
  auto node_type = stage_->get_node_type_info();

  // Verify node type
  EXPECT_EQ(node_type.type, NodeType::SOURCE);
  EXPECT_EQ(node_type.stage_name, "PAL_CVBS_Source");
  EXPECT_EQ(node_type.display_name, "PAL CVBS Source");

  // Verify connectivity
  EXPECT_EQ(node_type.min_inputs, 0);
  EXPECT_EQ(node_type.max_inputs, 0);
  EXPECT_EQ(node_type.min_outputs, 1);
}

TEST_F(CVBSSourceContractTest, StageIdentity_HasVersion) {
  EXPECT_EQ(stage_->version(), "1.0.0");
}

TEST_F(CVBSSourceContractTest, Stage_IdentityZeroRequiredInputs) {
  EXPECT_EQ(stage_->required_input_count(), 0);
}

// ====================
// Parameter Definition Tests
// ====================

TEST_F(CVBSSourceContractTest, Parameters_InputPathDescriptor) {
  auto descriptors = stage_->get_parameter_descriptors();
  ASSERT_GT(descriptors.size(), 0);

  // Find input_path parameter
  auto input_path_it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [](const ParameterDescriptor& d) { return d.name == "input_path"; });
  ASSERT_NE(input_path_it, descriptors.end());

  const auto& desc = *input_path_it;
  EXPECT_EQ(desc.name, "input_path");
  EXPECT_EQ(desc.display_name, "CVBS File Path");
  EXPECT_EQ(desc.type, ParameterType::FILE_PATH);
  EXPECT_FALSE(desc.constraints.required);
  EXPECT_EQ(desc.file_extension_hint, ".composite");
  if (!desc.constraints.default_value.has_value()) {
    FAIL() << "Expected default_value to have a value";
    return;
  }
  EXPECT_EQ(std::get<std::string>(*desc.constraints.default_value), "");
}

TEST_F(CVBSSourceContractTest, Parameters_UseMetadataDescriptor) {
  auto descriptors = stage_->get_parameter_descriptors();

  // Find use_metadata parameter
  auto use_metadata_it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [](const ParameterDescriptor& d) { return d.name == "use_metadata"; });
  ASSERT_NE(use_metadata_it, descriptors.end());

  const auto& desc = *use_metadata_it;
  EXPECT_EQ(desc.name, "use_metadata");
  EXPECT_EQ(desc.display_name, "Use Metadata");
  EXPECT_EQ(desc.type, ParameterType::BOOL);
  EXPECT_FALSE(desc.constraints.required);
  if (!desc.constraints.default_value.has_value()) {
    FAIL() << "Expected default_value to have a value";
    return;
  }
  EXPECT_EQ(std::get<bool>(*desc.constraints.default_value), true);
}

TEST_F(CVBSSourceContractTest, Parameters_SampleEncodingDescriptor) {
  auto descriptors = stage_->get_parameter_descriptors();

  // Find sample_encoding parameter
  auto encoding_it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [](const ParameterDescriptor& d) { return d.name == "sample_encoding"; });
  ASSERT_NE(encoding_it, descriptors.end());

  const auto& desc = *encoding_it;
  EXPECT_EQ(desc.name, "sample_encoding");
  EXPECT_EQ(desc.display_name, "Sample Encoding");
  EXPECT_EQ(desc.type, ParameterType::STRING);
  EXPECT_FALSE(desc.constraints.required);
  if (!desc.constraints.default_value.has_value()) {
    FAIL() << "Expected default_value to have a value";
    return;
  }
  EXPECT_EQ(std::get<std::string>(*desc.constraints.default_value),
            "CVBS_U16_4FSC");

  // Verify allowed values
  ASSERT_EQ(desc.constraints.allowed_strings.size(), 3);
  EXPECT_TRUE(std::find(desc.constraints.allowed_strings.begin(),
                        desc.constraints.allowed_strings.end(),
                        "CVBS_U16_4FSC") !=
              desc.constraints.allowed_strings.end());
  EXPECT_TRUE(std::find(desc.constraints.allowed_strings.begin(),
                        desc.constraints.allowed_strings.end(),
                        "CVBS_TPG21_4FSC") !=
              desc.constraints.allowed_strings.end());
  EXPECT_TRUE(std::find(desc.constraints.allowed_strings.begin(),
                        desc.constraints.allowed_strings.end(),
                        "CVBS_S16_FSC") !=
              desc.constraints.allowed_strings.end());
}

TEST_F(CVBSSourceContractTest, Parameters_SampleEncodingDependsOnUseMetadata) {
  auto descriptors = stage_->get_parameter_descriptors();

  auto encoding_it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [](const ParameterDescriptor& d) { return d.name == "sample_encoding"; });
  ASSERT_NE(encoding_it, descriptors.end());

  const auto& desc = *encoding_it;
  ASSERT_TRUE(desc.constraints.depends_on.has_value());
  EXPECT_EQ(desc.constraints.depends_on->parameter_name, "use_metadata");
  ASSERT_EQ(desc.constraints.depends_on->required_values.size(), 1u);
  EXPECT_EQ(desc.constraints.depends_on->required_values.front(), "false");
  // Must gray out, not hide, so the user can see the control is ignored.
  EXPECT_FALSE(desc.constraints.depends_on->hide_when_disabled);
}

TEST_F(CVBSSourceContractTest, Parameters_HasExactlyThreeParameters) {
  auto descriptors = stage_->get_parameter_descriptors();

  EXPECT_EQ(descriptors.size(), 3);

  // Verify all parameter names
  std::set<std::string> param_names;
  for (const auto& desc : descriptors) {
    param_names.insert(desc.name);
  }
  EXPECT_EQ(param_names.size(), 3);
  EXPECT_TRUE(param_names.count("input_path"));
  EXPECT_TRUE(param_names.count("use_metadata"));
  EXPECT_TRUE(param_names.count("sample_encoding"));
}

// ====================
// Parameter Setter/Getter Tests
// ====================

TEST_F(CVBSSourceContractTest, Parameter_SetGetInputPath) {
  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/path/to/file.cvbs");

  EXPECT_TRUE(stage_->set_parameters(params));

  auto retrieved = stage_->get_parameters();
  EXPECT_EQ(std::get<std::string>(retrieved["input_path"]),
            "/path/to/file.cvbs");
}

TEST_F(CVBSSourceContractTest, Parameter_SetGetUseMetadata) {
  std::map<std::string, ParameterValue> params;
  params["use_metadata"] = true;

  EXPECT_TRUE(stage_->set_parameters(params));

  auto retrieved = stage_->get_parameters();
  EXPECT_TRUE(std::get<bool>(retrieved["use_metadata"]));
}

TEST_F(CVBSSourceContractTest, Parameter_SetGetSampleEncoding) {
  std::map<std::string, ParameterValue> params;
  params["sample_encoding"] = std::string("CVBS_TPG21_4FSC");

  EXPECT_TRUE(stage_->set_parameters(params));

  auto retrieved = stage_->get_parameters();
  EXPECT_EQ(std::get<std::string>(retrieved["sample_encoding"]),
            "CVBS_TPG21_4FSC");
}

TEST_F(CVBSSourceContractTest, Parameter_SetGetAllParameters) {
  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/path/to/cvbs.cvbs");
  params["use_metadata"] = true;
  params["sample_encoding"] = std::string("CVBS_U16_4FSC");

  EXPECT_TRUE(stage_->set_parameters(params));

  auto retrieved = stage_->get_parameters();
  EXPECT_EQ(std::get<std::string>(retrieved["input_path"]),
            "/path/to/cvbs.cvbs");
  EXPECT_TRUE(std::get<bool>(retrieved["use_metadata"]));
  EXPECT_EQ(std::get<std::string>(retrieved["sample_encoding"]),
            "CVBS_U16_4FSC");
}

// ====================
// Manual Mode Validation Tests
// ====================

TEST_F(CVBSSourceContractTest, Manual_ModeValidationValidPALStageU16) {
  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/valid/path.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_U16_4FSC");

  EXPECT_TRUE(stage_->set_parameters(params));
  // Execution will throw UserDataError with clear message (Phase 2 not
  // implemented)
}

TEST(CVBSSourceNTSCContractTest, Manual_ModeValidationValidNTSCStageTPG21) {
  NTSCCVBSSourceStage stage;
  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/valid/path.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_TPG21_4FSC");

  EXPECT_TRUE(stage.set_parameters(params));
}

TEST_F(CVBSSourceContractTest, Manual_ModeValidationInvalidSampleEncoding) {
  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/path.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_INVALID_ENCODING");

  EXPECT_TRUE(stage_->set_parameters(params));
  // Execute should fail with validation error
}

TEST(CVBSSourceValidationTest,
     ManualMode_RejectsInvalidSampleEncodingAtExecute) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_INVALID_ENCODING");

  expect_user_data_error_contains(stage, params, "Invalid sample_encoding");
}

// ====================
// Metadata Mode Tests (Phase 1 Framework Only)
// ====================

TEST(CVBSSourceValidationTest, MetadataMode_ConstructsBasenameMetaPath) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/path/to/video.composite");
  params["use_metadata"] = true;

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  EXPECT_THROW((void)stage.execute(inputs, params, observation_context),
               UserDataError);
  EXPECT_EQ(deps->last_metadata_path, "/path/to/video.meta");
}

TEST(CVBSSourceValidationTest, MetadataMode_RejectsUnsupportedPreset) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->metadata_record.preset = "PAL_M";
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = true;

  expect_user_data_error_contains(stage, params, "Unsupported metadata preset");
}

TEST(CVBSSourceValidationTest, MetadataMode_RejectsUnsupportedSampleEncoding) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->metadata_record.sample_encoding_preset = "CVBS_U10_4FSC";
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = true;

  expect_user_data_error_contains(
      stage, params, "Unsupported metadata sample_encoding_preset");
}

TEST(CVBSSourceValidationTest, MetadataMode_RejectsUnsupportedSignalState) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->metadata_record.signal_state_preset = "STANDARD_TBC_UNLOCKED";
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = true;

  expect_user_data_error_contains(stage, params,
                                  "Unsupported metadata signal_state_preset");
}

TEST(CVBSSourceValidationTest, MetadataMode_RejectsNonCompositeSignalType) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->metadata_record.signal_type = "yc";
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = true;

  expect_user_data_error_contains(stage, params,
                                  "Unsupported metadata signal_type");
}

TEST(CVBSSourceValidationTest, MetadataMode_SurfacesMetadataReadFailure) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->metadata_available = false;
  deps->metadata_error = "metadata backend unavailable";
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = true;

  expect_user_data_error_contains(stage, params,
                                  "metadata backend unavailable");
}

TEST(CVBSSourceValidationTest, ManualMode_SurfacesInputValidationFailure) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->input_file_valid = false;
  deps->input_file_error = "CVBS source file not found";
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_U16_4FSC");

  expect_user_data_error_contains(stage, params, "CVBS source file not found");
}

TEST(CVBSSourceValidationTest,
     MetadataModeValidMetadata_ProducesRepresentation) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->metadata_record = CVBSMetadataRecord{
      "NTSC", "CVBS_TPG21_4FSC", "STANDARD_TBC_LOCKED", "composite"};
  const int16_t encoded = static_cast<int16_t>((300 - 508) * 64);
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(encoded));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = true;

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);
  EXPECT_EQ(representation->field_count(), 2u);

  auto first_field = representation->get_field(FieldID(0));
  ASSERT_FALSE(first_field.empty());
  EXPECT_EQ(first_field.front(), static_cast<uint16_t>(300 * 64));
}

TEST(CVBSSourceValidationTest, MetadataMode_AcceptsMatchingPalMetadata) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->metadata_record = CVBSMetadataRecord{
      "PAL", "CVBS_U16_4FSC", "STANDARD_TBC_LOCKED", "composite"};
  deps->payload_words.assign(static_cast<size_t>(709379),
                             static_cast<uint16_t>(256 * 64));
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.composite");
  params["use_metadata"] = true;

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);
  EXPECT_EQ(representation->field_count(), 2u);

  const auto video_params = representation->get_video_parameters();
  if (!video_params.has_value()) {
    FAIL() << "Expected video_params to have a value";
    return;
  }
  EXPECT_EQ(video_params->system, VideoSystem::PAL);
  EXPECT_EQ(video_params->field_width, 1135);
}

TEST(CVBSSourceValidationTest, MetadataMode_RejectsMismatchedVideoStandard) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->metadata_record = CVBSMetadataRecord{
      "NTSC", "CVBS_U16_4FSC", "STANDARD_TBC_LOCKED", "composite"};
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.composite");
  params["use_metadata"] = true;

  expect_user_data_error_contains(stage, params,
                                  "Unsupported metadata preset 'NTSC'");
}

TEST(CVBSSourceDecodeTest, ManualMode_DecodesU16IntoCommonDomain) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(252 * 64));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_U16_4FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);

  EXPECT_EQ(representation->field_count(), 2u);
  auto first_field = representation->get_field(FieldID(0));
  ASSERT_FALSE(first_field.empty());
  EXPECT_EQ(first_field.front(), static_cast<uint16_t>(252 * 64));

  auto video_params = representation->get_video_parameters();
  if (!video_params.has_value()) {
    FAIL() << "Expected video_params to have a value";
    return;
  }
  EXPECT_EQ(video_params->system, VideoSystem::NTSC);
  EXPECT_EQ(video_params->field_width, 910);
  EXPECT_EQ(video_params->number_of_sequential_fields, 2);
}

TEST(CVBSSourceDecodeTest, ManualMode_DecodesTPG21IntoCommonDomain) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  const int16_t encoded = static_cast<int16_t>((300 - 508) * 64);
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(encoded));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_TPG21_4FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);

  auto first_field = representation->get_field(FieldID(0));
  ASSERT_FALSE(first_field.empty());
  EXPECT_EQ(first_field.front(), static_cast<uint16_t>(300 * 64));
}

TEST(CVBSSourceDecodeTest, ManualMode_DecodesS16FSCNtscIntoCommonDomain) {
  // NTSC: blanking=240 (10-bit). Black level=252.
  // CVBS_S16_FSC: int16 = (252 - 240) * 32 = 384.
  // Expected internal domain: 252 * 64 = 16128.
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  const int16_t encoded = static_cast<int16_t>((252 - 240) * 32);
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(encoded));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_S16_FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);

  auto first_field = representation->get_field(FieldID(0));
  ASSERT_FALSE(first_field.empty());
  EXPECT_EQ(first_field.front(), static_cast<uint16_t>(252 * 64));
}

TEST(CVBSSourceDecodeTest, ManualMode_DecodesS16FSCPalGeometry) {
  // PAL: blanking=256 (10-bit). Black level=282.
  // CVBS_S16_FSC: int16 = (282 - 256) * 32 = 832.
  // PAL output goes through sinc resampling (fractional-line → uniform), so
  // exact sample-level comparisons at frame boundaries are unreliable. Verify
  // geometry and field count instead.
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  const int16_t encoded = static_cast<int16_t>((282 - 256) * 32);
  deps->payload_words.assign(static_cast<size_t>(709379),
                             static_cast<uint16_t>(encoded));
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_S16_FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);

  EXPECT_EQ(representation->field_count(), 2u);

  auto video_params = representation->get_video_parameters();
  ASSERT_TRUE(video_params.has_value());
  EXPECT_EQ(video_params->system, VideoSystem::PAL);
  EXPECT_EQ(video_params->field_width, 1135);
}

TEST(CVBSSourceDecodeTest, ManualMode_S16FSCBlankingMapsToCorrectInternalLevel) {
  // NTSC: blanking=240. Encoded blanking = (240 - 240) * 32 = 0.
  // Expected internal domain: 240 * 64 = 15360.
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(0));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_S16_FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);

  auto first_field = representation->get_field(FieldID(0));
  ASSERT_FALSE(first_field.empty());
  EXPECT_EQ(first_field.front(), static_cast<uint16_t>(240 * 64));
}

TEST(CVBSSourceDecodeTest,
     ManualMode_S16FSCSyncTipDecodesCorrectlyForNtsc) {
  // NTSC: blanking=240, sync tip=16.
  // CVBS_S16_FSC: int16 = (16 - 240) * 32 = -7168.
  // Expected internal domain: 16 * 64 = 1024.
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  const int16_t encoded = static_cast<int16_t>((16 - 240) * 32);
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(encoded));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_S16_FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);

  auto first_field = representation->get_field(FieldID(0));
  ASSERT_FALSE(first_field.empty());
  EXPECT_EQ(first_field.front(), static_cast<uint16_t>(16 * 64));
}

TEST(CVBSSourceValidationTest,
     MetadataMode_AcceptsS16FSCSampleEncoding) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->metadata_record = CVBSMetadataRecord{
      "NTSC", "CVBS_S16_FSC", "STANDARD_TBC_LOCKED", "composite"};
  // NTSC: blanking=240. Encoded blanking = 0.
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(0));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = true;

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);
  EXPECT_EQ(representation->field_count(), 2u);
}

TEST(CVBSSourceDecodeTest, ManualMode_ClampsLowTPG21SamplesIntoInternalDomain) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  const int16_t encoded_below_zero = static_cast<int16_t>(-509 * 64);
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(encoded_below_zero));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_TPG21_4FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);

  auto first_field = representation->get_field(FieldID(0));
  ASSERT_FALSE(first_field.empty());
  EXPECT_EQ(first_field.front(), static_cast<uint16_t>(0));
}

TEST(CVBSSourceDecodeTest, ManualMode_UsesPalFieldGeometry) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  // PAL frame: 355,257 (odd) + 354,122 (even) = 709,379 samples total
  // Odd field: 311 lines @ 1135 samples + 2 fractional lines (156, 312) @ 1136
  // = 355,257 Even field: 310 lines @ 1135 samples + 2 fractional lines (155,
  // 311) @ 1136 = 354,122
  deps->payload_words.assign(static_cast<size_t>(709379),
                             static_cast<uint16_t>(256 * 64));
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_U16_4FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);

  auto desc0 = representation->get_descriptor(FieldID(0));
  auto desc1 = representation->get_descriptor(FieldID(1));
  if (!desc0.has_value() || !desc1.has_value()) {
    FAIL() << "Expected both descriptors to have a value";
    return;
  }
  EXPECT_EQ(desc0->width, 1135u);
  EXPECT_EQ(desc0->height, 313u);
  EXPECT_EQ(desc1->height, 312u);
}

TEST(CVBSSourceDecodeTest, ManualMode_DropsIncompleteFrameTail) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  const size_t ntsc_frame_samples = static_cast<size_t>(910 * 525);
  deps->payload_words.assign(ntsc_frame_samples + 100,
                             static_cast<uint16_t>(240 * 64));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_U16_4FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);
  EXPECT_EQ(representation->field_count(), 2u);
}

TEST(CVBSSourceDecodeTest,
     ManualModeNtscDescriptorParity_MatchesTemporalOrder) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(240 * 64));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_U16_4FSC");

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);

  ASSERT_EQ(result.size(), 1u);
  auto representation =
      std::dynamic_pointer_cast<VideoFieldRepresentation>(result.front());
  ASSERT_TRUE(representation != nullptr);

  auto desc0 = representation->get_descriptor(FieldID(0));
  auto desc1 = representation->get_descriptor(FieldID(1));
  if (!desc0.has_value() || !desc1.has_value()) {
    FAIL() << "Expected both descriptors to have a value";
    return;
  }

  EXPECT_EQ(desc0->height, 263u);
  EXPECT_EQ(desc1->height, 262u);
  EXPECT_EQ(desc0->parity, FieldParity::Top);
  EXPECT_EQ(desc1->parity, FieldParity::Bottom);

  const auto parity0 = representation->get_field_parity_hint(FieldID(0));
  const auto parity1 = representation->get_field_parity_hint(FieldID(1));
  if (!parity0.has_value() || !parity1.has_value()) {
    FAIL() << "Expected both parity hints to have a value";
    return;
  }
  EXPECT_TRUE(parity0->is_first_field);
  EXPECT_FALSE(parity1->is_first_field);
}

// ====================
// No-Input Tests
// ====================

TEST_F(CVBSSourceContractTest, ExecuteSourceStage_HasNoInputs) {
  std::vector<ArtifactPtr> inputs;
  std::map<std::string, ParameterValue> parameters;
  // Empty file path -> no execution attempted
  parameters["input_path"] = std::string("");

  orc::ObservationContext observation_context;
  auto result = stage_->execute(inputs, parameters, observation_context);
  EXPECT_TRUE(result.empty());
}

// ====================
// Empty Path Handling
// ====================

TEST_F(CVBSSourceContractTest, ExecuteEmptyInputPath_ReturnsEmpty) {
  std::vector<ArtifactPtr> inputs;
  std::map<std::string, ParameterValue> parameters;
  parameters["input_path"] = std::string("");

  orc::ObservationContext observation_context;
  auto result = stage_->execute(inputs, parameters, observation_context);
  EXPECT_EQ(result.size(), 0);
}

// ====================
// Legacy Contract Behavior Tests
// ====================

TEST_F(CVBSSourceContractTest, ExecuteInvalidPath_SurfacesUserDataError) {
  std::vector<ArtifactPtr> inputs;
  std::map<std::string, ParameterValue> parameters;
  parameters["input_path"] = std::string("/some/valid/path.cvbs");
  parameters["use_metadata"] = false;
  parameters["sample_encoding"] = std::string("CVBS_U16_4FSC");

  orc::ObservationContext observation_context;
  // Invalid filesystem path should still surface as UserDataError.
  EXPECT_THROW(stage_->execute(inputs, parameters, observation_context),
               UserDataError);
}

TEST_F(CVBSSourceContractTest, Preview_EmptyBeforeExecute) {
  EXPECT_FALSE(stage_->supports_preview());
  EXPECT_TRUE(stage_->get_preview_options().empty());
}

TEST(CVBSSourcePreviewTest, Preview_AvailableAfterSuccessfulExecute) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  deps->payload_words.assign(static_cast<size_t>(910 * 525),
                             static_cast<uint16_t>(252 * 64));
  NTSCCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_U16_4FSC");
  ASSERT_TRUE(stage.set_parameters(params));

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);
  ASSERT_EQ(result.size(), 1u);

  EXPECT_TRUE(stage.supports_preview());
  const auto options = stage.get_preview_options();
  EXPECT_FALSE(options.empty());

  const auto has_field_option = std::any_of(
      options.begin(), options.end(),
      [](const PreviewOption& option) { return option.id == "field"; });
  EXPECT_TRUE(has_field_option);
}

TEST(CVBSSourceReportTest, GenerateReport_ProvidesConfiguredFields) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>();
  // PAL frame: 355,257 (odd) + 354,122 (even) = 709,379 samples total
  deps->payload_words.assign(static_cast<size_t>(709379),
                             static_cast<uint16_t>(256 * 64));
  PALCVBSSourceStage stage(deps);

  std::map<std::string, ParameterValue> params;
  params["input_path"] = std::string("/virtual/input.cvbs");
  params["use_metadata"] = false;
  params["sample_encoding"] = std::string("CVBS_U16_4FSC");
  ASSERT_TRUE(stage.set_parameters(params));

  std::vector<ArtifactPtr> inputs;
  ObservationContext observation_context;
  const auto result = stage.execute(inputs, params, observation_context);
  ASSERT_EQ(result.size(), 1u);

  const auto report = stage.generate_report();
  if (!report.has_value()) {
    FAIL() << "Expected report to have a value";
    return;
  }

  const auto has_source_file = std::any_of(
      report->items.begin(), report->items.end(), [](const auto& item) {
        return item.first == "Source File" &&
               item.second == "/virtual/input.cvbs";
      });
  EXPECT_TRUE(has_source_file);

  const auto has_mode = std::any_of(
      report->items.begin(), report->items.end(), [](const auto& item) {
        return item.first == "Mode" && item.second == "Manual";
      });
  EXPECT_TRUE(has_mode);
}

}  // namespace tests
}  // namespace orc
