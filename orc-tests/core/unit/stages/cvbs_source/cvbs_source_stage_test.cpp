/*
 * File:        cvbs_source_stage_test.cpp
 * Module:      orc-tests/core/unit/stages/cvbs_source
 * Purpose:     Unit tests for PAL, NTSC, and PAL_M CVBS source stages
 *
 * Tests: stage identity, signal state validation, encoding normalisation,
 * SourceParameters from spec constants, sidecar loading (dropout / audio /
 * EFM / AC3), and NTSC-J black level.
 * All I/O is fully mocked through ICVBSSourceStageDeps.
 *
 * Colour burst phase measurement is not performed by the source stage;
 * see colour_frame_phase_observer_test.cpp.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../../../../orc/plugins/stages/cvbs_source/cvbs_source_stage.h"

#include <cvbs_signal_constants.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../../../orc/common/include/common_types.h"
#include "../../../../orc/common/include/error_types.h"
#include "../../../../orc/common/include/parameter_types.h"
#include "../../../../orc/core/include/dropout_run.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/core/include/video_frame_representation.h"

namespace orc {
namespace tests {

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a payload where every sample = blanking (normalised output = blanking).
// Encoded as CVBS_U16_4FSC (blanking × 64).
std::vector<uint16_t> make_blank_payload_u16(size_t word_count,
                                             int32_t blanking_10bit) {
  return std::vector<uint16_t>(word_count,
                               static_cast<uint16_t>(blanking_10bit * 64));
}

// ---------------------------------------------------------------------------
// FakeCVBSSourceStageDeps — fully in-memory fake, no I/O
// ---------------------------------------------------------------------------

class FakeCVBSSourceStageDeps final : public ICVBSSourceStageDeps {
 public:
  // --- validation ---
  bool input_file_valid = true;
  std::string input_file_error = "Input file error";

  // --- metadata ---
  bool metadata_available = true;
  std::string metadata_error = "Metadata read error";
  CVBSMetadataRecord metadata_record{};
  mutable std::string last_metadata_path;

  // --- payload words ---
  std::vector<uint16_t> payload_words;

  // --- dropout sidecar ---
  std::vector<DropoutRun> dropout_runs;

  // --- audio sidecar ---
  std::optional<CVBSAudioSidecarInfo> audio_info;  // nullopt = absent
  std::vector<int16_t> audio_samples;  // flat buffer of all stereo pairs

  // --- EFM sidecar ---
  std::optional<std::vector<CVBSExtensionFrameRef>> efm_table;
  std::vector<uint8_t> efm_data;

  // --- AC3 sidecar ---
  std::optional<std::vector<CVBSExtensionFrameRef>> ac3_table;
  std::vector<uint8_t> ac3_data;

  explicit FakeCVBSSourceStageDeps(
      const std::string& preset = "PAL",
      const std::string& encoding = "CVBS_U16_4FSC") {
    metadata_record.preset = preset;
    metadata_record.sample_encoding_preset = encoding;
    metadata_record.signal_state_preset = "STANDARD_TBC_LOCKED";
    metadata_record.signal_type = "composite";
    metadata_record.number_of_sequential_frames = 1;

    // Default payload: one PAL frame at blanking in CVBS_U16_4FSC.
    payload_words = make_blank_payload_u16(
        static_cast<size_t>(kPalFrameSamples), kPalBlanking);
  }

  bool validate_input_file(const std::string& /*path*/,
                           std::string& error_message) const override {
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
      const std::string& /*path*/, std::string& error_message) const override {
    if (payload_words.empty()) {
      error_message = "Empty payload";
      return std::nullopt;
    }
    return payload_words.size();
  }

  bool read_input_words_at(const std::string& /*path*/, size_t word_offset,
                           size_t word_count, std::vector<uint16_t>& out_words,
                           std::string& error_message) const override {
    if (word_offset + word_count > payload_words.size()) {
      error_message = "Read out of bounds";
      return false;
    }
    out_words.assign(
        payload_words.begin() + static_cast<std::ptrdiff_t>(word_offset),
        payload_words.begin() +
            static_cast<std::ptrdiff_t>(word_offset + word_count));
    return true;
  }

  std::vector<DropoutRun> load_dropout_sidecar(
      const std::string& /*path*/,
      std::string& /*error_message*/) const override {
    return dropout_runs;
  }

  std::optional<CVBSAudioSidecarInfo> get_audio_info(
      const std::string& /*path*/) const override {
    return audio_info;
  }

  std::vector<int16_t> read_audio_samples_at(
      const std::string& /*path*/, size_t stereo_pair_offset,
      size_t stereo_pair_count) const override {
    const size_t start = stereo_pair_offset * 2;
    const size_t end =
        std::min(start + stereo_pair_count * 2, audio_samples.size());
    if (start >= audio_samples.size()) return {};
    return std::vector<int16_t>(
        audio_samples.begin() + static_cast<std::ptrdiff_t>(start),
        audio_samples.begin() + static_cast<std::ptrdiff_t>(end));
  }

  std::optional<std::vector<CVBSExtensionFrameRef>> load_efm_frame_table(
      const std::string& /*path*/,
      std::string& /*error_message*/) const override {
    return efm_table;
  }

  std::vector<uint8_t> read_efm_bytes_at(const std::string& /*path*/,
                                         uint64_t byte_offset,
                                         uint32_t count) const override {
    const size_t start = static_cast<size_t>(byte_offset);
    const size_t end = std::min(start + count, efm_data.size());
    if (start >= efm_data.size()) return {};
    return std::vector<uint8_t>(
        efm_data.begin() + static_cast<std::ptrdiff_t>(start),
        efm_data.begin() + static_cast<std::ptrdiff_t>(end));
  }

  std::optional<std::vector<CVBSExtensionFrameRef>> load_ac3_frame_table(
      const std::string& /*path*/,
      std::string& /*error_message*/) const override {
    return ac3_table;
  }

  std::vector<uint8_t> read_ac3_bytes_at(const std::string& /*path*/,
                                         uint64_t byte_offset,
                                         uint32_t count) const override {
    const size_t start = static_cast<size_t>(byte_offset);
    const size_t end = std::min(start + count, ac3_data.size());
    if (start >= ac3_data.size()) return {};
    return std::vector<uint8_t>(
        ac3_data.begin() + static_cast<std::ptrdiff_t>(start),
        ac3_data.begin() + static_cast<std::ptrdiff_t>(end));
  }
};

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

VideoFrameRepresentationPtr execute_and_get_vfr(
    DAGStage& stage, const std::map<std::string, ParameterValue>& params) {
  std::vector<ArtifactPtr> inputs;
  ObservationContext obs;
  auto results = stage.execute(inputs, params, obs);
  EXPECT_EQ(results.size(), 1u);
  if (results.empty()) return nullptr;
  return std::dynamic_pointer_cast<VideoFrameRepresentation>(results.front());
}

void expect_user_data_error(DAGStage& stage,
                            const std::map<std::string, ParameterValue>& params,
                            const std::string& msg_fragment) {
  std::vector<ArtifactPtr> inputs;
  ObservationContext obs;
  try {
    stage.execute(inputs, params, obs);
    ADD_FAILURE() << "Expected UserDataError containing: " << msg_fragment;
  } catch (const UserDataError& e) {
    EXPECT_NE(std::string(e.what()).find(msg_fragment), std::string::npos)
        << "Actual error: " << e.what();
  }
}

const std::map<std::string, ParameterValue> kDefaultParams{
    {"input_path", std::string("/fake/video.composite")}};

}  // namespace

// ===========================================================================
// Stage identity
// ===========================================================================

TEST(CVBSSourceStageIdentityTest, PAL_HasCorrectStageName) {
  PALCVBSSourceStage stage;
  EXPECT_EQ(stage.get_node_type_info().stage_name, "PAL_CVBS_Source");
}

TEST(CVBSSourceStageIdentityTest, NTSC_HasCorrectStageName) {
  NTSCCVBSSourceStage stage;
  EXPECT_EQ(stage.get_node_type_info().stage_name, "NTSC_CVBS_Source");
}

TEST(CVBSSourceStageIdentityTest, PALM_HasCorrectStageName) {
  PALMCVBSSourceStage stage;
  EXPECT_EQ(stage.get_node_type_info().stage_name, "PALM_CVBS_Source");
}

TEST(CVBSSourceStageIdentityTest, PAL_NodeTypeIsSource) {
  PALCVBSSourceStage stage;
  EXPECT_EQ(stage.get_node_type_info().type, NodeType::SOURCE);
}

TEST(CVBSSourceStageIdentityTest, Stage_RequiredInputCountIsZero) {
  PALCVBSSourceStage stage;
  EXPECT_EQ(stage.required_input_count(), 0u);
}

TEST(CVBSSourceStageIdentityTest, Stage_VersionIsCurrentPhase) {
  PALCVBSSourceStage stage;
  EXPECT_EQ(stage.version(), "2.0.0");
}

// ===========================================================================
// Parameter descriptors
// ===========================================================================

TEST(CVBSSourceStageParamTest, HasExactlyOneParameter) {
  PALCVBSSourceStage stage;
  auto descs = stage.get_parameter_descriptors();
  ASSERT_EQ(descs.size(), 1u);
  EXPECT_EQ(descs[0].name, "input_path");
}

TEST(CVBSSourceStageParamTest, InputPath_IsFilePathType) {
  PALCVBSSourceStage stage;
  auto descs = stage.get_parameter_descriptors();
  EXPECT_EQ(descs[0].type, ParameterType::FILE_PATH);
}

TEST(CVBSSourceStageParamTest, InputPath_DefaultIsEmptyString) {
  PALCVBSSourceStage stage;
  auto descs = stage.get_parameter_descriptors();
  ASSERT_TRUE(descs[0].constraints.default_value.has_value());
  EXPECT_EQ(std::get<std::string>(*descs[0].constraints.default_value), "");
}

TEST(CVBSSourceStageParamTest, SetGet_InputPath_RoundTrips) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  std::map<std::string, ParameterValue> p{
      {"input_path", std::string("/test/video.composite")}};
  ASSERT_TRUE(stage.set_parameters(p));
  auto got = stage.get_parameters();
  EXPECT_EQ(std::get<std::string>(got["input_path"]), "/test/video.composite");
}

TEST(CVBSSourceStageParamTest, SetParameters_RejectsUnknownKey) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  std::map<std::string, ParameterValue> p{{"no_such_param", std::string("x")}};
  EXPECT_FALSE(stage.set_parameters(p));
}

// ===========================================================================
// Signal state validation
// ===========================================================================

TEST(CVBSSourceStageValidationTest, RejectsNonTBCLockedState) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  deps->metadata_record.signal_state_preset = "FREE_RUNNING";
  PALCVBSSourceStage stage(deps);
  expect_user_data_error(stage, kDefaultParams, "STANDARD_TBC_LOCKED");
}

TEST(CVBSSourceStageValidationTest, RejectsMissingMetadata) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  deps->metadata_available = false;
  PALCVBSSourceStage stage(deps);
  expect_user_data_error(stage, kDefaultParams, "metadata");
}

TEST(CVBSSourceStageValidationTest, RejectsWrongVideoStandard) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  deps->metadata_record.preset = "NTSC";  // wrong system for PAL stage
  PALCVBSSourceStage stage(deps);
  expect_user_data_error(stage, kDefaultParams, "NTSC");
}

TEST(CVBSSourceStageValidationTest, RejectsInvalidInputFile) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  deps->input_file_valid = false;
  PALCVBSSourceStage stage(deps);
  expect_user_data_error(stage, kDefaultParams, "Input file error");
}

TEST(CVBSSourceStageValidationTest, EmptyInputPath_ReturnsNoOutput) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  std::vector<ArtifactPtr> inputs;
  ObservationContext obs;
  std::map<std::string, ParameterValue> p{{"input_path", std::string("")}};
  auto results = stage.execute(inputs, p, obs);
  EXPECT_TRUE(results.empty());
}

TEST(CVBSSourceStageValidationTest, AcceptsAllFourSampleEncodings) {
  for (const auto& enc :
       {"CVBS_U10_4FSC", "CVBS_U16_4FSC", "CVBS_TPG21_4FSC", "CVBS_S16_FSC"}) {
    auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL", enc);
    // For U10 encoding, payload must be int16_t stored as uint16_t.
    // Blanking=256 encoded as uint16_t bit-pattern of int16_t(256) = 256.
    if (std::string(enc) == "CVBS_U10_4FSC") {
      deps->payload_words.assign(static_cast<size_t>(kPalFrameSamples),
                                 static_cast<uint16_t>(kPalBlanking));
    } else if (std::string(enc) == "CVBS_S16_FSC") {
      // CVBS_S16_FSC: value=blanking stored as (value-blanking)*32 = 0.
      deps->payload_words.assign(static_cast<size_t>(kPalFrameSamples), 0u);
    } else if (std::string(enc) == "CVBS_TPG21_4FSC") {
      // CVBS_TPG21_4FSC: value=blanking stored as (value-508)*64 = (256-508)*64
      const int16_t stored = static_cast<int16_t>((kPalBlanking - 508) * 64);
      deps->payload_words.assign(static_cast<size_t>(kPalFrameSamples),
                                 static_cast<uint16_t>(stored));
    }
    PALCVBSSourceStage stage(deps);
    auto vfr = execute_and_get_vfr(stage, kDefaultParams);
    ASSERT_NE(vfr, nullptr) << "Failed for encoding: " << enc;
    EXPECT_EQ(vfr->frame_count(), 1u) << "Failed for encoding: " << enc;
  }
}

// ===========================================================================
// Sample encoding normalisation
// ===========================================================================

// Helper: decode one frame with a given encoding and return the decoded sample
// value at a specific word index.
int16_t decode_one_sample(const std::string& preset, uint16_t raw_word,
                          const std::string& encoding, int32_t frame_samples) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>(preset, encoding);
  deps->payload_words.assign(static_cast<size_t>(frame_samples), raw_word);
  deps->metadata_record.sample_encoding_preset = encoding;
  deps->metadata_record.number_of_sequential_frames = 1;

  std::shared_ptr<FixedFormatCVBSSourceStage> stage;
  if (preset == "PAL") {
    stage = std::make_shared<PALCVBSSourceStage>(deps);
  } else if (preset == "NTSC") {
    stage = std::make_shared<NTSCCVBSSourceStage>(deps);
  } else {
    stage = std::make_shared<PALMCVBSSourceStage>(deps);
  }

  auto vfr = execute_and_get_vfr(*stage, kDefaultParams);
  if (!vfr) return 0;
  const auto* line = vfr->get_line(0, 0);
  return line ? line[0] : 0;
}

TEST(CVBSSourceEncodingTest, U10_IdentityTransform) {
  // CVBS_U10_4FSC: raw uint16_t reinterpreted as int16_t.
  const int16_t expected = 256;  // PAL blanking
  const uint16_t raw = static_cast<uint16_t>(expected);
  EXPECT_EQ(decode_one_sample("PAL", raw, "CVBS_U10_4FSC", kPalFrameSamples),
            expected);
}

TEST(CVBSSourceEncodingTest, U10_PreservesHeadroomAboveWhite) {
  // Raw 1023 (just below peak) encoded as uint16_t bit-pattern of
  // int16_t(1023).
  const int16_t expected = 1023;
  const uint16_t raw = static_cast<uint16_t>(expected);
  EXPECT_EQ(decode_one_sample("PAL", raw, "CVBS_U10_4FSC", kPalFrameSamples),
            expected);
}

TEST(CVBSSourceEncodingTest, U16_DividesBy64) {
  // PAL blanking = 256. Stored as 256 * 64 = 16384.
  const uint16_t raw = static_cast<uint16_t>(kPalBlanking * 64);
  EXPECT_EQ(decode_one_sample("PAL", raw, "CVBS_U16_4FSC", kPalFrameSamples),
            static_cast<int16_t>(kPalBlanking));
}

TEST(CVBSSourceEncodingTest, U16_WhiteLevelRoundtrips) {
  const int32_t white_10bit = kPalWhite;
  const uint16_t raw = static_cast<uint16_t>(white_10bit * 64);
  EXPECT_EQ(decode_one_sample("PAL", raw, "CVBS_U16_4FSC", kPalFrameSamples),
            static_cast<int16_t>(kPalWhite));
}

TEST(CVBSSourceEncodingTest, TPG21_BlankingLevelRoundtrips) {
  // CVBS_TPG21_4FSC: stored = (value - 508) * 64
  const int32_t value = kPalBlanking;  // 256
  const int16_t stored = static_cast<int16_t>((value - 508) * 64);
  const uint16_t raw = static_cast<uint16_t>(stored);
  EXPECT_EQ(decode_one_sample("PAL", raw, "CVBS_TPG21_4FSC", kPalFrameSamples),
            static_cast<int16_t>(kPalBlanking));
}

TEST(CVBSSourceEncodingTest, TPG21_WhiteLevelRoundtrips) {
  const int32_t value = kPalWhite;  // 844
  const int16_t stored = static_cast<int16_t>((value - 508) * 64);
  const uint16_t raw = static_cast<uint16_t>(stored);
  EXPECT_EQ(decode_one_sample("PAL", raw, "CVBS_TPG21_4FSC", kPalFrameSamples),
            static_cast<int16_t>(kPalWhite));
}

TEST(CVBSSourceEncodingTest, S16_BlankingLevelRoundtrips) {
  // CVBS_S16_FSC: stored = (value - blanking) * 32. At blanking → stored = 0.
  EXPECT_EQ(decode_one_sample("PAL", 0u, "CVBS_S16_FSC", kPalFrameSamples),
            static_cast<int16_t>(kPalBlanking));
}

TEST(CVBSSourceEncodingTest, S16_WhiteLevelRoundtrips) {
  // PAL white = 844; (844-256)*32 = 18816.
  const int16_t stored = static_cast<int16_t>((kPalWhite - kPalBlanking) * 32);
  const uint16_t raw = static_cast<uint16_t>(stored);
  EXPECT_EQ(decode_one_sample("PAL", raw, "CVBS_S16_FSC", kPalFrameSamples),
            static_cast<int16_t>(kPalWhite));
}

// ===========================================================================
// SourceParameters from spec constants
// ===========================================================================

TEST(CVBSSourceParamsTest, PAL_FrameWidthNominalIs1135) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto params = vfr->get_video_parameters();
  ASSERT_TRUE(params.has_value());
  EXPECT_EQ(params->frame_width_nominal, kPalMaxSamplesPerLine - 1);
}

TEST(CVBSSourceParamsTest, PAL_FrameHeightIs625) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto params = vfr->get_video_parameters();
  ASSERT_TRUE(params.has_value());
  EXPECT_EQ(params->frame_height, kPalFrameLines);
}

TEST(CVBSSourceParamsTest, PAL_LevelsMatchSpecConstants) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto p = vfr->get_video_parameters();
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->sync_tip_level, kPalSyncTip);
  EXPECT_EQ(p->blanking_level, kPalBlanking);
  EXPECT_EQ(p->black_level, kPalBlack);
  EXPECT_EQ(p->white_level, kPalWhite);
  EXPECT_EQ(p->peak_level, kPalPeak);
}

TEST(CVBSSourceParamsTest, PAL_LevelsNotFromMetadata) {
  // Even if metadata were to provide different level values, the source
  // MUST populate SourceParameters from cvbs_signal_constants.h.
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  deps->metadata_record.ntsc_j_black_level = 200;  // only valid for NTSC
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto p = vfr->get_video_parameters();
  ASSERT_TRUE(p.has_value());
  // PAL black level must always be the spec value.
  EXPECT_EQ(p->black_level, kPalBlack);
}

TEST(CVBSSourceParamsTest, NTSC_LevelsMatchSpecConstants) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("NTSC");
  deps->payload_words = make_blank_payload_u16(
      static_cast<size_t>(kNtscFrameSamples), kNtscBlanking);
  NTSCCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto p = vfr->get_video_parameters();
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->sync_tip_level, kNtscSyncTip);
  EXPECT_EQ(p->blanking_level, kNtscBlanking);
  EXPECT_EQ(p->black_level, kNtscBlack);
  EXPECT_EQ(p->white_level, kNtscWhite);
  EXPECT_EQ(p->peak_level, kNtscPeak);
}

TEST(CVBSSourceParamsTest, PALM_FrameWidthIs909) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL_M");
  deps->metadata_record.preset = "PAL_M";
  deps->payload_words = make_blank_payload_u16(
      static_cast<size_t>(kPalMFrameSamples), kNtscBlanking);
  PALMCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto p = vfr->get_video_parameters();
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->frame_width_nominal, kPalMSamplesPerLine);
}

TEST(CVBSSourceParamsTest, FrameCountMatchesMetadata) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  deps->metadata_record.number_of_sequential_frames = 3;
  deps->payload_words = make_blank_payload_u16(
      static_cast<size_t>(kPalFrameSamples) * 3, kPalBlanking);
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_EQ(vfr->frame_count(), 3u);
  auto p = vfr->get_video_parameters();
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->number_of_sequential_frames, 3);
}

// ===========================================================================
// Colour-frame phase: source stage does not measure
// ===========================================================================
// The source stage loads raw samples only; colour-frame phase measurement is
// delegated to ColourFramePhaseObserver.  See colour_frame_phase_observer_test.

TEST(CVBSSourcePhaseContractTest, ColourFrameIndex_IsUnknown) {
  // The source stage always reports colour_frame_index = -1 regardless of
  // burst content; measurement is ColourFramePhaseObserver's responsibility.
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto desc = vfr->get_frame_descriptor(0);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->colour_frame_index, -1);
}

TEST(CVBSSourcePhaseContractTest, FramePhaseHint_IsNullopt) {
  // The source stage provides no phase hint; it is set by
  // ColourFramePhaseObserver.
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_FALSE(vfr->get_frame_phase_hint(0).has_value());
}

// ===========================================================================
// VFR navigation contract
// ===========================================================================

TEST(CVBSVFRTest, FrameRange_StartsAtZero) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  const auto range = vfr->frame_range();
  EXPECT_EQ(range.first, 0u);
  EXPECT_EQ(range.last, 1u);
}

TEST(CVBSVFRTest, HasFrame_TrueForValidFrameID) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_TRUE(vfr->has_frame(0));
  EXPECT_FALSE(vfr->has_frame(1));
}

TEST(CVBSVFRTest, GetFrameDescriptor_ReturnsCorrectHeight) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto desc = vfr->get_frame_descriptor(0);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->height, static_cast<size_t>(kPalFrameLines));
}

TEST(CVBSVFRTest, GetFrameDescriptor_ReturnsCorrectSampleCount) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto desc = vfr->get_frame_descriptor(0);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->samples_total, static_cast<size_t>(kPalFrameSamples));
}

TEST(CVBSVFRTest, GetFrame_ReturnsNonNullPointerForValidFrame) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_NE(vfr->get_frame(0), nullptr);
  EXPECT_EQ(vfr->get_frame(1), nullptr);
}

TEST(CVBSVFRTest, GetLine_ReturnsNonNullForValidLine) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_NE(vfr->get_line(0, 0), nullptr);
  EXPECT_NE(vfr->get_line(0, kPalFrameLines - 1), nullptr);
  EXPECT_EQ(vfr->get_line(0, kPalFrameLines), nullptr);
}

TEST(CVBSVFRTest, GetFrameCopy_SizeMatchesFrameSamples) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto copy = vfr->get_frame_copy(0);
  EXPECT_EQ(copy.size(), static_cast<size_t>(kPalFrameSamples));
}

TEST(CVBSVFRTest, PAL_Line0StartsAtOffset0) {
  // PAL line 0 offset = 0 (first 1135-sample line).
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL", "CVBS_U10_4FSC");
  // Store a sentinel value only at position 0.
  deps->payload_words.assign(static_cast<size_t>(kPalFrameSamples),
                             static_cast<uint16_t>(kPalBlanking));
  deps->payload_words[0] = static_cast<uint16_t>(500);  // sentinel
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  const auto* line0 = vfr->get_line(0, 0);
  ASSERT_NE(line0, nullptr);
  EXPECT_EQ(line0[0], int16_t{500});
}

// ===========================================================================
// Execute() caches the representation
// ===========================================================================

TEST(CVBSSourceCacheTest, SecondExecuteReturnsSameArtifact) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  std::vector<ArtifactPtr> inputs;
  ObservationContext obs;
  auto r1 = stage.execute(inputs, kDefaultParams, obs);
  auto r2 = stage.execute(inputs, kDefaultParams, obs);
  ASSERT_EQ(r1.size(), 1u);
  ASSERT_EQ(r2.size(), 1u);
  EXPECT_EQ(r1.front().get(), r2.front().get());
}

// ===========================================================================
// Sidecar: dropout
// ===========================================================================

TEST(CVBSSidecarDropoutTest, NoDropoutSidecar_GetDropoutHintsEmpty) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  // dropout_runs default = empty
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_TRUE(vfr->get_dropout_hints(0).empty());
}

TEST(CVBSSidecarDropoutTest, WithDropoutRuns_HintsReturnedForCorrectFrame) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  deps->metadata_record.number_of_sequential_frames = 2;
  deps->payload_words = make_blank_payload_u16(
      static_cast<size_t>(kPalFrameSamples) * 2, kPalBlanking);

  DropoutRun run0;
  run0.frame_id = 0;
  run0.sample_start = 100;
  run0.sample_count = 50;
  run0.severity = 1;
  DropoutRun run1;
  run1.frame_id = 1;
  run1.sample_start = 200;
  run1.sample_count = 20;
  run1.severity = 2;
  deps->dropout_runs = {run0, run1};

  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);

  auto hints0 = vfr->get_dropout_hints(0);
  ASSERT_EQ(hints0.size(), 1u);
  EXPECT_EQ(hints0[0].sample_start, 100u);

  auto hints1 = vfr->get_dropout_hints(1);
  ASSERT_EQ(hints1.size(), 1u);
  EXPECT_EQ(hints1[0].sample_start, 200u);
}

// ===========================================================================
// Sidecar: audio
// ===========================================================================

TEST(CVBSSidecarAudioTest, NoAudioSidecar_HasAudioFalse) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  // audio_info = nullopt (default)
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_FALSE(vfr->has_audio());
  EXPECT_FALSE(vfr->audio_locked());
}

TEST(CVBSSidecarAudioTest, AudioPresent_FreeRunning_HasAudioTrue_LockedFalse) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  deps->audio_info = CVBSAudioSidecarInfo{false};
  deps->metadata_record.audio_locked = false;
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_TRUE(vfr->has_audio());
  EXPECT_FALSE(vfr->audio_locked());
  EXPECT_TRUE(vfr->get_audio_samples(0).empty());
}

TEST(CVBSSidecarAudioTest, AudioPresent_FrameLocked_ReturnsCorrectSamples) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  deps->audio_info = CVBSAudioSidecarInfo{true};
  deps->metadata_record.audio_locked = true;
  // PAL: 1764 stereo pairs per frame = 3528 int16_t values.
  deps->audio_samples.assign(3528, int16_t{42});
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_TRUE(vfr->has_audio());
  EXPECT_TRUE(vfr->audio_locked());
  EXPECT_EQ(vfr->get_audio_sample_count(0), 1764u);
  auto samples = vfr->get_audio_samples(0);
  ASSERT_EQ(samples.size(), 3528u);
  EXPECT_EQ(samples[0], int16_t{42});
}

// ===========================================================================
// Sidecar: EFM
// ===========================================================================

TEST(CVBSSidecarEFMTest, NoEFMSidecar_HasEfmFalse) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  // efm_table = nullopt (default)
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_FALSE(vfr->has_efm());
  EXPECT_EQ(vfr->get_efm_sample_count(0), 0u);
  EXPECT_TRUE(vfr->get_efm_samples(0).empty());
}

TEST(CVBSSidecarEFMTest, WithEFMSidecar_ReturnsCorrectBytes) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  CVBSExtensionFrameRef ref0;
  ref0.offset = 0;
  ref0.count = 5;
  deps->efm_table = std::vector<CVBSExtensionFrameRef>{ref0};
  deps->efm_data = {0x01, 0x02, 0x03, 0x04, 0x05};
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_TRUE(vfr->has_efm());
  EXPECT_EQ(vfr->get_efm_sample_count(0), 5u);
  auto bytes = vfr->get_efm_samples(0);
  ASSERT_EQ(bytes.size(), 5u);
  EXPECT_EQ(bytes[0], uint8_t{0x01});
  EXPECT_EQ(bytes[4], uint8_t{0x05});
}

// ===========================================================================
// Sidecar: AC3 RF
// ===========================================================================

TEST(CVBSSidecarAC3Test, NoAC3Sidecar_HasAc3False) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_FALSE(vfr->has_ac3_rf());
  EXPECT_EQ(vfr->get_ac3_symbol_count(0), 0u);
  EXPECT_TRUE(vfr->get_ac3_symbols(0).empty());
}

TEST(CVBSSidecarAC3Test, WithAC3Sidecar_ReturnsCorrectBytes) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  CVBSExtensionFrameRef ref0;
  ref0.offset = 0;
  ref0.count = 3;
  deps->ac3_table = std::vector<CVBSExtensionFrameRef>{ref0};
  deps->ac3_data = {0xAA, 0xBB, 0xCC};
  PALCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_TRUE(vfr->has_ac3_rf());
  EXPECT_EQ(vfr->get_ac3_symbol_count(0), 3u);
  auto bytes = vfr->get_ac3_symbols(0);
  ASSERT_EQ(bytes.size(), 3u);
  EXPECT_EQ(bytes[1], uint8_t{0xBB});
}

// ===========================================================================
// NTSC-J black level override
// ===========================================================================

TEST(CVBSNTSCJTest, NTSCWithBlackLevelOverride_FrameDescriptorHasOverride) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("NTSC");
  deps->metadata_record.ntsc_j_black_level = 230;  // Japanese NTSC 0 IRE
  deps->payload_words = make_blank_payload_u16(
      static_cast<size_t>(kNtscFrameSamples), kNtscBlanking);
  NTSCCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto desc = vfr->get_frame_descriptor(0);
  ASSERT_TRUE(desc.has_value());
  ASSERT_TRUE(desc->black_level_override.has_value());
  EXPECT_EQ(*desc->black_level_override, 230);
}

TEST(CVBSNTSCJTest, NTSCWithBlackLevelOverride_SourceParamsHasNonstandardFlag) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("NTSC");
  deps->metadata_record.ntsc_j_black_level = 230;
  deps->payload_words = make_blank_payload_u16(
      static_cast<size_t>(kNtscFrameSamples), kNtscBlanking);
  NTSCCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto p = vfr->get_video_parameters();
  ASSERT_TRUE(p.has_value());
  EXPECT_TRUE(p->has_nonstandard_values);
  EXPECT_EQ(p->black_level, 230);
}

TEST(CVBSNTSCJTest, NTSCWithoutBlackLevelOverride_UsesSpecDefault) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("NTSC");
  deps->payload_words = make_blank_payload_u16(
      static_cast<size_t>(kNtscFrameSamples), kNtscBlanking);
  NTSCCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  auto p = vfr->get_video_parameters();
  ASSERT_TRUE(p.has_value());
  EXPECT_FALSE(p->has_nonstandard_values);
  EXPECT_EQ(p->black_level, kNtscBlack);
}

// ===========================================================================
// PAL_M stage
// ===========================================================================

TEST(CVBSPALMTest, PALMStage_CanLoadPALMFile) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL_M");
  deps->metadata_record.preset = "PAL_M";
  deps->payload_words = make_blank_payload_u16(
      static_cast<size_t>(kPalMFrameSamples), kNtscBlanking);
  PALMCVBSSourceStage stage(deps);
  auto vfr = execute_and_get_vfr(stage, kDefaultParams);
  ASSERT_NE(vfr, nullptr);
  EXPECT_EQ(vfr->frame_count(), 1u);
  auto desc = vfr->get_frame_descriptor(0);
  ASSERT_TRUE(desc.has_value());
  EXPECT_EQ(desc->system, VideoSystem::PAL_M);
  EXPECT_EQ(desc->samples_total, static_cast<size_t>(kPalMFrameSamples));
}

TEST(CVBSPALMTest, PALMStage_RejectsNTSCFile) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL_M");
  deps->metadata_record.preset = "NTSC";  // wrong
  deps->payload_words = make_blank_payload_u16(
      static_cast<size_t>(kPalMFrameSamples), kNtscBlanking);
  PALMCVBSSourceStage stage(deps);
  expect_user_data_error(stage, kDefaultParams, "NTSC");
}

// ===========================================================================
// Preview and report (smoke tests)
// ===========================================================================

TEST(CVBSPreviewTest, SupportsPreview_OnlyAfterLoad) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  EXPECT_FALSE(stage.supports_preview());
  execute_and_get_vfr(stage, kDefaultParams);
  EXPECT_TRUE(stage.supports_preview());
}

TEST(CVBSPreviewTest, GetPreviewOptions_ReturnsOptionsAfterLoad) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  execute_and_get_vfr(stage, kDefaultParams);
  auto options = stage.get_preview_options();
  EXPECT_FALSE(options.empty());
}

TEST(CVBSReportTest, GenerateReport_NotLoadedShowsNotConfigured) {
  PALCVBSSourceStage stage;
  auto report = stage.generate_report();
  ASSERT_TRUE(report.has_value());
  bool found = false;
  for (const auto& item : report->items) {
    if (item.first == "Status" && item.second == "No input file path set") {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST(CVBSReportTest, GenerateReport_AfterLoadShowsFrameCount) {
  auto deps = std::make_shared<FakeCVBSSourceStageDeps>("PAL");
  PALCVBSSourceStage stage(deps);
  execute_and_get_vfr(stage, kDefaultParams);
  auto report = stage.generate_report();
  ASSERT_TRUE(report.has_value());
  EXPECT_NE(report->metrics.find("frame_count"), report->metrics.end());
  EXPECT_EQ(std::get<int64_t>(report->metrics.at("frame_count")), int64_t{1});
}

}  // namespace tests
}  // namespace orc
