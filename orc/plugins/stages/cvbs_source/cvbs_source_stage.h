/*
 * File:        cvbs_source_stage.h
 * Module:      orc-core
 * Purpose:     CVBS (Composite Video Baseband Signal) source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef CVBS_SOURCE_STAGE_H
#define CVBS_SOURCE_STAGE_H

#include <stage_parameter.h>
#include <video_field_representation.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../../../sdk/include/orc/plugin/orc_stage_preview.h"
#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"

namespace orc {

struct CVBSStageIdentity {
  const char* stage_name;
  const char* display_name;
  const char* description;
  VideoFormatCompatibility compatible_formats;
  const char* fixed_video_standard;
};

struct CVBSMetadataRecord {
  std::string preset;
  std::string sample_encoding_preset;
  std::string signal_state_preset;
  std::string signal_type;
};

class ICVBSSourceStageDeps {
 public:
  virtual ~ICVBSSourceStageDeps() = default;

  virtual bool validate_input_file(const std::string& input_path,
                                   std::string& error_message) const = 0;
  virtual std::optional<CVBSMetadataRecord> load_metadata(
      const std::string& meta_path, std::string& error_message) const = 0;

  // Returns the total number of 16-bit words in the file without loading it
  // into memory.
  virtual std::optional<size_t> get_input_word_count(
      const std::string& input_path, std::string& error_message) const = 0;

  // Reads exactly word_count 16-bit words starting at word_offset from the
  // file.
  virtual bool read_input_words_at(const std::string& input_path,
                                   size_t word_offset, size_t word_count,
                                   std::vector<uint16_t>& out_words,
                                   std::string& error_message) const = 0;
};

/**
 * @brief Shared fixed-format CVBS source implementation
 *
 * This shared implementation backs the PAL and NTSC concrete CVBS source
 * stages. Each concrete stage hard-wires a single video standard and matches
 * the existing TBC source-stage pattern where project format selects the stage
 * type rather than a manual in-stage PAL/NTSC parameter.
 *
 * **Signal State Constraint:** Only `STANDARD_TBC_LOCKED` signal state is
 * supported. Files with other signal states (e.g., STANDARD_TBC_UNLOCKED,
 * STANDARD_RAW) will be rejected with clear validation errors.
 *
 * **Sample Encoding Support:**
 * - `CVBS_TPG21_4FSC` - Device-encoded composite (requires offset/scale
 * reversal)
 * - `CVBS_U16_4FSC` - 16-bit unsigned linear composite encoding
 *
 * **Operating Modes:**
 *
 * 1. **Metadata-Driven Mode** (`use_metadata=true`):
 *    - Reads optional `.meta` sidecar file alongside the CVBS data file
 *    - Derives sample encoding from metadata
 *    - Validates metadata video standard against the stage's fixed format
 *    - Ignores extension metadata (only core CVBS fields are used)
 *    - User provides: input path
 *    - Ignored parameters: sample_encoding
 *
 * 2. **Manual Mode** (`use_metadata=false`):
 *    - Stage format is fixed by the concrete stage type
 *    - User explicitly selects sample encoding
 *    - No metadata file required
 *    - User provides: input path, sample_encoding
 *    - Metadata file is ignored even if present
 *
 * **Output Contract:**
 * All decoded composite samples are normalized to an internal numeric domain
 * (phase adjustments for TPG21, value scaling for U16) before being split
 * into field-domain VideoFieldRepresentation for downstream consumption.
 * Each field carries stable identity metadata (frame index, field-in-frame
 * index, standard context) for deterministic processing.
 *
 * **Parameters:**
 */
class FixedFormatCVBSSourceStage : public DAGStage,
                                   public ParameterizedStage,
                                   public PreviewableStage {
 public:
  explicit FixedFormatCVBSSourceStage(
      CVBSStageIdentity identity,
      std::shared_ptr<ICVBSSourceStageDeps> deps = nullptr);
  ~FixedFormatCVBSSourceStage() override = default;

  void set_deps_override(std::shared_ptr<ICVBSSourceStageDeps> deps) {
    deps_override_ = std::move(deps);
  }

  // DAGStage interface
  std::string version() const override { return "1.0.0"; }

  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::SOURCE,
                        identity_.stage_name,
                        identity_.display_name,
                        identity_.description,
                        0,
                        0,  // No inputs
                        1,
                        UINT32_MAX,  // Many outputs (fields)
                        identity_.compatible_formats,
                        SinkCategory::CORE,
                        "Source"};
  }

  std::vector<ArtifactPtr> execute(
      const std::vector<ArtifactPtr>& inputs,
      const std::map<std::string, ParameterValue>& parameters,
      ObservationContext& observation_context) override;

  size_t required_input_count() const override {
    return 0;
  }  // Source has no inputs
  size_t output_count() const override { return 1; }

  // ParameterizedStage interface
  std::vector<ParameterDescriptor> get_parameter_descriptors(
      VideoSystem project_format, SourceType source_type) const override;
  using ParameterizedStage::get_parameter_descriptors;

  std::map<std::string, ParameterValue> get_parameters() const override;
  bool set_parameters(
      const std::map<std::string, ParameterValue>& params) override;

  // PreviewableStage interface
  bool supports_preview() const override;
  std::vector<PreviewOption> get_preview_options() const override;
  PreviewImage render_preview(const std::string& option_id, uint64_t index,
                              PreviewNavigationHint hint) const override;

  // Stage inspection
  std::optional<StageReport> generate_report() const override;

 private:
  CVBSStageIdentity identity_;

  // Storage for current parameter values
  std::string input_path_;
  bool use_metadata_ = false;
  std::string
      sample_encoding_;  // "CVBS_TPG21_4FSC" or "CVBS_U16_4FSC" (manual mode)

  // Cache the loaded representation to avoid reloading
  mutable std::string cached_input_path_;
  mutable std::shared_ptr<VideoFieldRepresentation> cached_representation_;
  std::shared_ptr<ICVBSSourceStageDeps> deps_override_;

  // Validation helper - checks signal state and other metadata constraints
  // Returns error message if invalid, empty string if valid
  std::string validate_metadata_mode(
      const std::string& input_path, const std::string& meta_path,
      std::string& resolved_sample_encoding) const;

  // Validation helper - checks manual mode selections
  std::string validate_manual_mode(const std::string& sample_encoding) const;
};

class PALCVBSSourceStage final : public FixedFormatCVBSSourceStage {
 public:
  explicit PALCVBSSourceStage(
      std::shared_ptr<ICVBSSourceStageDeps> deps = nullptr);
  ~PALCVBSSourceStage() override = default;
};

class NTSCCVBSSourceStage final : public FixedFormatCVBSSourceStage {
 public:
  explicit NTSCCVBSSourceStage(
      std::shared_ptr<ICVBSSourceStageDeps> deps = nullptr);
  ~NTSCCVBSSourceStage() override = default;
};

}  // namespace orc

#endif  // CVBS_SOURCE_STAGE_H
