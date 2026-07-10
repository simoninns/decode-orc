/*
 * File:        tbc_source_stage_test.cpp
 * Module:      orc-tests/core/unit/stages/tbc_source
 * Purpose:     Unit tests for TBCSourceStage parameter descriptors, execute
 *              contract, and DI-based loading. All I/O is fully mocked through
 *              ITBCSourceStageDeps.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/tbc_source/tbc_source_stage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/error_types.h>
#include <orc/stage/node_type.h>
#include <orc/stage/observation_context.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../source_common/source_stage_descriptor_test_utils.h"

using testing::_;  // NOLINT(bugprone-reserved-identifier)
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace orc_unit_test {

// ===========================================================================
// Mock for ITBCSourceStageDeps
// ===========================================================================

class MockTBCSourceStageDeps : public orc::ITBCSourceStageDeps {
 public:
  MOCK_METHOD(bool, validate_input_file,
              (const std::string& path, std::string& error_message),
              (const, override));

  MOCK_METHOD(std::optional<orc::TBCVideoParams>, load_video_params,
              (const std::string& db_path, std::string& error_message),
              (const, override));

  MOCK_METHOD(std::vector<orc::TBCFieldMeta>, load_all_field_meta,
              (const std::string& db_path, std::string& error_message),
              (const, override));

  MOCK_METHOD(std::vector<uint16_t>, read_field_samples,
              (const std::string& tbc_path, int32_t field_index,
               int32_t stored_samples_per_field, int32_t use_sample_count,
               std::string& error_message),
              (const, override));

  MOCK_METHOD(std::vector<uint16_t>, read_field_samples_at,
              (const std::string& tbc_path, int32_t field_index,
               int32_t stored_samples_per_field, int32_t sample_offset,
               int32_t use_sample_count, std::string& error_message),
              (const, override));

  MOCK_METHOD(bool, has_audio_file, (const std::string& pcm_path),
              (const, override));

  MOCK_METHOD(std::vector<int16_t>, read_audio_samples_at,
              (const std::string& pcm_path, size_t stereo_pair_offset,
               size_t stereo_pair_count),
              (const, override));

  MOCK_METHOD(bool, has_efm_file, (const std::string& efm_bin_path),
              (const, override));

  MOCK_METHOD(std::vector<uint8_t>, read_efm_bytes_at,
              (const std::string& efm_bin_path, size_t efm_byte_offset,
               size_t efm_byte_count),
              (const, override));

  MOCK_METHOD(bool, has_ac3_files,
              (const std::string& ac3_bin_path,
               const std::string& ac3_meta_path),
              (const, override));

  MOCK_METHOD(std::optional<std::vector<uint8_t>>, read_ac3_for_frame,
              (const std::string& ac3_bin_path,
               const std::string& ac3_meta_path, int32_t field_seq_no_a,
               int32_t field_seq_no_b),
              (const, override));
};

// ===========================================================================
// Helper builders
// ===========================================================================

// Build a minimal PAL TBCVideoParams.
static orc::TBCVideoParams make_pal_video_params(int32_t num_fields = 2) {
  orc::TBCVideoParams tvp;
  tvp.system = orc::VideoSystem::PAL;
  tvp.number_of_fields = num_fields;
  tvp.field_width = orc::kPalSamplesPerLineNominal;                // 1135
  tvp.field1_height = orc::kPalField1Lines;                        // 313
  tvp.field2_height = orc::kPalFrameLines - orc::kPalField1Lines;  // 312
  tvp.blanking_16b = 16384;
  tvp.white_16b = 54400;
  return tvp;
}

// Build two minimal PAL TBCFieldMeta entries (one frame).
static std::vector<orc::TBCFieldMeta> make_pal_field_meta(
    int32_t num_fields = 2) {
  std::vector<orc::TBCFieldMeta> meta(static_cast<size_t>(num_fields));
  for (int i = 0; i < num_fields; ++i) {
    meta[static_cast<size_t>(i)].field_phase_id = (i % 8) + 1;
  }
  return meta;
}

// Return a blanking-level field of the given size.
static std::vector<uint16_t> make_blanking_field(int32_t sample_count) {
  return std::vector<uint16_t>(static_cast<size_t>(sample_count),
                               static_cast<uint16_t>(16384));
}

// Build a minimal NTSC TBCVideoParams (audio on NTSC is resampled; PAL is not).
static orc::TBCVideoParams make_ntsc_video_params(int32_t num_fields = 2) {
  orc::TBCVideoParams tvp;
  tvp.system = orc::VideoSystem::NTSC;
  tvp.number_of_fields = num_fields;
  tvp.field_width = orc::kNtscSamplesPerLine;                        // 910
  tvp.field1_height = orc::kNtscField1Lines;                         // 263
  tvp.field2_height = orc::kNtscFrameLines - orc::kNtscField1Lines;  // 262
  tvp.blanking_16b = 16384;
  tvp.white_16b = 54400;
  return tvp;
}

// Build NTSC TBCFieldMeta entries, optionally carrying a per-field audio pair
// count so the source exposes lockable audio.
static std::vector<orc::TBCFieldMeta> make_ntsc_field_meta(
    int32_t num_fields, int32_t audio_pairs_per_field) {
  std::vector<orc::TBCFieldMeta> meta(static_cast<size_t>(num_fields));
  for (int i = 0; i < num_fields; ++i) {
    meta[static_cast<size_t>(i)].field_phase_id = (i % 4) + 1;
    if (audio_pairs_per_field > 0) {
      meta[static_cast<size_t>(i)].audio_sample_count = audio_pairs_per_field;
    }
  }
  return meta;
}

// ===========================================================================
// Parameter descriptor tests
// ===========================================================================

TEST(TBCSourceStageTest, ParameterDescriptors_ContainsInputPath) {
  orc::TBCSourceStage stage;
  auto descs = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descs, "input_path", ".tbc");
}

TEST(TBCSourceStageTest, ParameterDescriptors_ContainsYPath) {
  orc::TBCSourceStage stage;
  auto descs = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descs, "y_path", ".tbcy");
}

TEST(TBCSourceStageTest, ParameterDescriptors_ContainsCPath) {
  orc::TBCSourceStage stage;
  auto descs = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descs, "c_path", ".tbcc");
}

TEST(TBCSourceStageTest, ParameterDescriptors_ContainsPcmPath) {
  orc::TBCSourceStage stage;
  auto descs = stage.get_parameter_descriptors();
  expect_file_path_descriptor(descs, "pcm_path", ".pcm");
}

TEST(TBCSourceStageTest, ParameterDescriptors_AllAreOptional) {
  orc::TBCSourceStage stage;
  auto descs = stage.get_parameter_descriptors();
  expect_all_descriptors_optional(descs);
}

TEST(TBCSourceStageTest, Descriptor_DefaultsInputPathIsEmptyString) {
  orc::TBCSourceStage stage;
  auto descs = stage.get_parameter_descriptors();
  expect_empty_string_default(descs, "input_path");
}

// ===========================================================================
// NodeTypeInfo / stage identity
// ===========================================================================

TEST(TBCSourceStageTest, NodeTypeInfo_StageNameIsTBCSource) {
  const orc::TBCSourceStage stage;
  EXPECT_EQ(stage.get_node_type_info().stage_name, "tbc_source");
}

TEST(TBCSourceStageTest, NodeTypeInfo_CompatibilityIsAll) {
  const orc::TBCSourceStage stage;
  EXPECT_EQ(stage.get_node_type_info().compatible_formats,
            orc::VideoFormatCompatibility::ALL);
}

TEST(TBCSourceStageTest, NodeTypeInfo_ZeroInputsOneOutput) {
  const orc::TBCSourceStage stage;
  EXPECT_EQ(stage.get_node_type_info().min_inputs, 0u);
  EXPECT_EQ(stage.get_node_type_info().max_inputs, 0u);
  EXPECT_GE(stage.get_node_type_info().max_outputs, 1u);
}

// ===========================================================================
// set_parameters validation
// ===========================================================================

TEST(TBCSourceStageTest, SetParameters_AcceptsValidStringMap) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault(Return(make_pal_video_params()));
  orc::TBCSourceStage stage(deps);
  EXPECT_TRUE(
      stage.set_parameters({{"input_path", std::string("/tmp/test.tbc")},
                            {"pcm_path", std::string("")}}));
}

TEST(TBCSourceStageTest, SetParameters_RejectsNonStringValues) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  EXPECT_FALSE(
      stage.set_parameters({{"input_path", static_cast<int32_t>(42)}}));
}

TEST(TBCSourceStageTest, SetParameters_AcceptsEmptyMap) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  EXPECT_TRUE(stage.set_parameters({}));
}

// ===========================================================================
// execute() contract
// ===========================================================================

TEST(TBCSourceStageTest, Execute_ThrowsWhenInputsProvided) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  EXPECT_THROW(stage.execute({nullptr}, {}, ctx), std::runtime_error);
}

TEST(TBCSourceStageTest, Execute_ReturnsEmptyWhenNoInputPathConfigured) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  auto outputs = stage.execute({}, {}, ctx);
  EXPECT_TRUE(outputs.empty());
}

TEST(TBCSourceStageTest, Execute_ReturnsEmptyWhenInputPathIsEmptyString) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  auto outputs = stage.execute({}, {{"input_path", std::string("")}}, ctx);
  EXPECT_TRUE(outputs.empty());
}

TEST(TBCSourceStageTest, Execute_ThrowsUserDataErrorWhenFileNotFound) {
  auto deps = std::make_shared<StrictMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  EXPECT_CALL(*deps, validate_input_file(_, _))
      .WillOnce([](const std::string&, std::string& err) {
        err = "File not found";
        return false;
      });

  EXPECT_THROW(stage.execute(
                   {}, {{"input_path", std::string("/no/such/file.tbc")}}, ctx),
               orc::UserDataError);
}

TEST(TBCSourceStageTest, Execute_ThrowsUserDataErrorWhenMetadataFails) {
  auto deps = std::make_shared<StrictMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  EXPECT_CALL(*deps, validate_input_file(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*deps, load_video_params(_, _))
      .WillOnce([](const std::string&, std::string& err) {
        err = "DB error";
        return std::optional<orc::TBCVideoParams>{};
      });
  EXPECT_CALL(*deps, has_audio_file(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*deps, has_efm_file(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*deps, has_ac3_files(_, _)).WillRepeatedly(Return(false));

  EXPECT_THROW(
      stage.execute({}, {{"input_path", std::string("/tmp/test.tbc")}}, ctx),
      orc::UserDataError);
}

TEST(TBCSourceStageTest,
     Execute_ReturnsSingleOutputWhenPALMetadataLoadSucceeds) {
  // Use NiceMock: this test focuses on the return value, not call counts on
  // sidecar-check methods (which may or may not be called depending on whether
  // paths are set).
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return std::optional<orc::TBCVideoParams>{make_pal_video_params(2)};
      });
  ON_CALL(*deps, load_all_field_meta(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return make_pal_field_meta(2);
      });
  ON_CALL(*deps, has_audio_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_efm_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_ac3_files(_, _)).WillByDefault(Return(false));

  const auto outputs =
      stage.execute({}, {{"input_path", std::string("/tmp/test.tbc")}}, ctx);

  ASSERT_EQ(outputs.size(), 1u);
  EXPECT_NE(outputs.front(), nullptr);
}

TEST(TBCSourceStageTest, Execute_DisplayNameSetToPALTBCCompositeAfterLoad) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return std::optional<orc::TBCVideoParams>{make_pal_video_params(2)};
      });
  ON_CALL(*deps, load_all_field_meta(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return make_pal_field_meta(2);
      });
  ON_CALL(*deps, has_audio_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_efm_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_ac3_files(_, _)).WillByDefault(Return(false));

  stage.execute({}, {{"input_path", std::string("/tmp/test.tbc")}}, ctx);

  EXPECT_EQ(stage.get_node_type_info().display_name, "PAL TBC Composite");
}

// ===========================================================================
// VideoFrameRepresentation output contract
// ===========================================================================

TEST(TBCSourceStageTest, OutputIsVFR_FrameCountIsFieldCountDividedByTwo) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  constexpr int32_t kNumFields = 4;

  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return std::optional<orc::TBCVideoParams>{
            make_pal_video_params(kNumFields)};
      });
  ON_CALL(*deps, load_all_field_meta(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return make_pal_field_meta(kNumFields);
      });
  ON_CALL(*deps, has_audio_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_efm_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_ac3_files(_, _)).WillByDefault(Return(false));

  const auto outputs =
      stage.execute({}, {{"input_path", std::string("/tmp/test.tbc")}}, ctx);

  ASSERT_EQ(outputs.size(), 1u);
  const auto* vfr =
      dynamic_cast<orc::VideoFrameRepresentation*>(outputs.front().get());
  ASSERT_NE(vfr, nullptr);
  EXPECT_EQ(vfr->frame_count(), static_cast<size_t>(kNumFields / 2));
}

TEST(TBCSourceStageTest, OutputVFR_GetFrameLazilyAssemblesFromMockedDeps) {
  // Verify that get_frame() triggers read_field_samples() via deps for frame 0.
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  constexpr int32_t kF1Samples =
      orc::kPalField1Lines * (orc::kPalSamplesPerLineNominal);  // 313 × 1135
  constexpr int32_t kF2Samples =
      (orc::kPalFrameLines - orc::kPalField1Lines) *
      (orc::kPalSamplesPerLineNominal);  // 312 × 1135

  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return std::optional<orc::TBCVideoParams>{make_pal_video_params(2)};
      });
  ON_CALL(*deps, load_all_field_meta(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return make_pal_field_meta(2);
      });
  ON_CALL(*deps, has_audio_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_efm_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_ac3_files(_, _)).WillByDefault(Return(false));

  // Expect read_field_samples for TBC fields 0 and 1 (frame 0).
  EXPECT_CALL(*deps, read_field_samples("/tmp/test.tbc", 0, _, kF1Samples, _))
      .WillOnce([kF1Samples](const std::string&, int32_t, int32_t, int32_t,
                             std::string&) {
        return make_blanking_field(kF1Samples);
      });
  EXPECT_CALL(*deps, read_field_samples("/tmp/test.tbc", 1, _, kF2Samples, _))
      .WillOnce([kF2Samples](const std::string&, int32_t, int32_t, int32_t,
                             std::string&) {
        return make_blanking_field(kF2Samples);
      });

  const auto outputs =
      stage.execute({}, {{"input_path", std::string("/tmp/test.tbc")}}, ctx);

  ASSERT_EQ(outputs.size(), 1u);
  auto* vfr =
      dynamic_cast<orc::VideoFrameRepresentation*>(outputs.front().get());
  ASSERT_NE(vfr, nullptr);

  // Trigger lazy loading.
  const auto* frame_ptr = vfr->get_frame(0);
  ASSERT_NE(frame_ptr, nullptr);

  // All samples must be at kPalBlanking (TBC blanking maps to CVBS blanking).
  EXPECT_EQ(frame_ptr[0], static_cast<int16_t>(orc::kPalBlanking));
}

// ===========================================================================
// set_parameters status feedback tests
// ===========================================================================

TEST(TBCSourceStageStatusTest, SetParameters_ShowsRed_WhenPathEmpty) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  EXPECT_TRUE(stage.set_parameters({}));
  EXPECT_EQ(stage.get_configuration_status(), orc::ConfigurationStatus::Red);
}

TEST(TBCSourceStageStatusTest, SetParameters_ShowsRed_WhenFileNotAccessible) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(false));
  orc::TBCSourceStage stage(deps);
  std::map<std::string, orc::ParameterValue> params;
  params["input_path"] = std::string("/path/to/video.tbc");
  EXPECT_TRUE(stage.set_parameters(params));
  EXPECT_EQ(stage.get_configuration_status(), orc::ConfigurationStatus::Red);
}

TEST(TBCSourceStageStatusTest,
     SetParameters_ShowsYellow_WhenMetadataNotAccessible) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _)).WillByDefault(Return(std::nullopt));
  orc::TBCSourceStage stage(deps);
  std::map<std::string, orc::ParameterValue> params;
  params["input_path"] = std::string("/path/to/video.tbc");
  EXPECT_TRUE(stage.set_parameters(params));
  EXPECT_EQ(stage.get_configuration_status(), orc::ConfigurationStatus::Yellow);
}

TEST(TBCSourceStageStatusTest,
     SetParameters_ShowsGreen_WhenFileAndMetadataAccessible) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault(Return(make_pal_video_params()));
  orc::TBCSourceStage stage(deps);
  std::map<std::string, orc::ParameterValue> params;
  params["input_path"] = std::string("/path/to/video.tbc");
  EXPECT_TRUE(stage.set_parameters(params));
  EXPECT_EQ(stage.get_configuration_status(), orc::ConfigurationStatus::Green);
}

TEST(TBCSourceStageStatusTest,
     SetParameters_ShowsGreen_WhenYCModeAndMetadataAccessible) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault(Return(make_pal_video_params()));
  orc::TBCSourceStage stage(deps);
  std::map<std::string, orc::ParameterValue> params;
  params["y_path"] = std::string("/path/to/video_y.tbc");
  params["c_path"] = std::string("/path/to/video_c.tbc");
  EXPECT_TRUE(stage.set_parameters(params));
  EXPECT_EQ(stage.get_configuration_status(), orc::ConfigurationStatus::Green);
}

// ===========================================================================
// EFM sidecar loading (issue #210)
// ===========================================================================

// A TBC source with a raw .efm sidecar (no .efm.meta index — that is CVBS-only)
// must expose EFM data.  Per-field T-value counts come from the TBC metadata;
// the .efm file stores one byte per T-value in field order.
TEST(TBCSourceStageTest, OutputVFR_ExposesEFM_WhenEfmFileAndMetadataCountsSet) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  constexpr int32_t kNumFields = 4;  // two frames
  // Per-field T-value counts: frame 0 = fields 0+1 = 10+5 = 15 bytes @ off 0;
  //                           frame 1 = fields 2+3 = 8+3 = 11 bytes @ off 15.
  const std::array<int32_t, 4> kCounts = {10, 5, 8, 3};

  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return std::optional<orc::TBCVideoParams>{
            make_pal_video_params(kNumFields)};
      });
  ON_CALL(*deps, load_all_field_meta(_, _))
      .WillByDefault([kCounts](const std::string&, std::string&) {
        auto meta = make_pal_field_meta(kNumFields);
        for (size_t i = 0; i < meta.size(); ++i) {
          meta[i].efm_t_value_count = kCounts[i];
        }
        return meta;
      });
  ON_CALL(*deps, has_audio_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_efm_file(_)).WillByDefault(Return(true));
  ON_CALL(*deps, has_ac3_files(_, _)).WillByDefault(Return(false));

  // Return a synthetic payload that encodes the requested (offset, count) so
  // the test can assert both the byte range and the concatenation.
  ON_CALL(*deps, read_efm_bytes_at(_, _, _))
      .WillByDefault([](const std::string&, size_t offset, size_t count) {
        std::vector<uint8_t> bytes(count);
        for (size_t i = 0; i < count; ++i) {
          bytes[i] = static_cast<uint8_t>((offset + i) & 0xFF);
        }
        return bytes;
      });

  const auto outputs =
      stage.execute({},
                    {{"input_path", std::string("/tmp/test.tbc")},
                     {"efm_path", std::string("/tmp/test.efm")}},
                    ctx);

  ASSERT_EQ(outputs.size(), 1u);
  const auto* vfr =
      dynamic_cast<orc::VideoFrameRepresentation*>(outputs.front().get());
  ASSERT_NE(vfr, nullptr);
  EXPECT_TRUE(vfr->has_efm());

  // Per-frame T-value counts must be reported (EFM Sink sums these to size its
  // buffer; a zero here is what regressed as "no EFM t-values found").
  EXPECT_EQ(vfr->get_efm_sample_count(0), 15u);
  EXPECT_EQ(vfr->get_efm_sample_count(1), 11u);

  // Frame 0: 15 bytes starting at offset 0.
  const auto frame0 = vfr->get_efm_samples(0);
  ASSERT_EQ(frame0.size(), 15u);
  EXPECT_EQ(frame0.front(), 0u);
  EXPECT_EQ(frame0.back(), 14u);

  // Frame 1: 11 bytes starting at offset 15 (sum of frame 0's field counts).
  const auto frame1 = vfr->get_efm_samples(1);
  ASSERT_EQ(frame1.size(), 11u);
  EXPECT_EQ(frame1.front(), 15u);
  EXPECT_EQ(frame1.back(), 25u);
}

// When the .efm file exists but the metadata carries no T-value counts, EFM is
// unavailable — there is no way to index the raw stream without counts.
TEST(TBCSourceStageTest, OutputVFR_NoEFM_WhenEfmFileButMetadataCountsAbsent) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return std::optional<orc::TBCVideoParams>{make_pal_video_params(2)};
      });
  ON_CALL(*deps, load_all_field_meta(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return make_pal_field_meta(2);  // no efm_t_value_count set
      });
  ON_CALL(*deps, has_audio_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_efm_file(_)).WillByDefault(Return(true));
  ON_CALL(*deps, has_ac3_files(_, _)).WillByDefault(Return(false));

  const auto outputs =
      stage.execute({},
                    {{"input_path", std::string("/tmp/test.tbc")},
                     {"efm_path", std::string("/tmp/test.efm")}},
                    ctx);

  ASSERT_EQ(outputs.size(), 1u);
  const auto* vfr =
      dynamic_cast<orc::VideoFrameRepresentation*>(outputs.front().get());
  ASSERT_NE(vfr, nullptr);
  EXPECT_FALSE(vfr->has_efm());
}

// ===========================================================================
// NTSC/PAL_M audio resample is deferred (issue #209)
// ===========================================================================

// Building the source representation is the video-preview / getAvailableOutputs
// hot path and never needs audio.  The NTSC/PAL_M resample reads the whole PCM
// and runs a full SoXR pass, so doing it eagerly during execute() stalled the
// render worker for minutes on long NTSC sources ("stuck on rendering").  It
// must be deferred to the first audio access.
TEST(TBCSourceStageTest, NtscAudio_ResampleDeferredUntilFirstAudioAccess) {
  auto deps = std::make_shared<NiceMock<MockTBCSourceStageDeps>>();
  orc::TBCSourceStage stage(deps);
  orc::ObservationContext ctx;

  constexpr int32_t kNumFields = 4;        // two frames
  constexpr int32_t kPairsPerField = 735;  // ~NTSC audio pairs per field
  constexpr size_t kTotalPairs =
      static_cast<size_t>(kNumFields) * kPairsPerField;

  ON_CALL(*deps, validate_input_file(_, _)).WillByDefault(Return(true));
  ON_CALL(*deps, load_video_params(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return std::optional<orc::TBCVideoParams>{
            make_ntsc_video_params(kNumFields)};
      });
  ON_CALL(*deps, load_all_field_meta(_, _))
      .WillByDefault([](const std::string&, std::string&) {
        return make_ntsc_field_meta(kNumFields, kPairsPerField);
      });
  ON_CALL(*deps, has_audio_file(_)).WillByDefault(Return(true));
  ON_CALL(*deps, has_efm_file(_)).WillByDefault(Return(false));
  ON_CALL(*deps, has_ac3_files(_, _)).WillByDefault(Return(false));

  // Building the representation must not read the PCM.
  EXPECT_CALL(*deps, read_audio_samples_at(_, _, _)).Times(0);

  const auto outputs =
      stage.execute({},
                    {{"input_path", std::string("/tmp/test.tbc")},
                     {"pcm_path", std::string("/tmp/test.pcm")}},
                    ctx);

  ASSERT_EQ(outputs.size(), 1u);
  auto* vfr =
      dynamic_cast<orc::VideoFrameRepresentation*>(outputs.front().get());
  ASSERT_NE(vfr, nullptr);
  // The track descriptor is a cheap metadata query — it must not trigger the
  // resample.
  const auto track_desc = vfr->get_audio_track_descriptor(0);
  ASSERT_TRUE(track_desc.has_value());
  EXPECT_TRUE(track_desc->locked);
  EXPECT_EQ(track_desc->origin, orc::AudioTrackOrigin::ANALOGUE);

  // The eager read would have fired by now; confirm it did not, then arm the
  // expectation for the deferred, one-shot read.
  ::testing::Mock::VerifyAndClearExpectations(deps.get());

  // First audio access reads the entire PCM exactly once and resamples it.
  EXPECT_CALL(*deps, read_audio_samples_at("/tmp/test.pcm", 0u, kTotalPairs))
      .Times(1)
      .WillOnce([](const std::string&, size_t, size_t count) {
        return std::vector<int16_t>(count * 2, 0);
      });

  const auto samples0 = vfr->get_audio_samples(0, 0);
  EXPECT_FALSE(samples0.empty());

  // A second access reuses the cached resample — no further PCM reads (the
  // Times(1) expectation above would fail otherwise).
  const auto samples1 = vfr->get_audio_samples(0, 1);
  EXPECT_FALSE(samples1.empty());
}

}  // namespace orc_unit_test
