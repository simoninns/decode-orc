/*
 * File:        ntsc_comp_source_stage.h
 * Module:      orc-core
 * Purpose:     NTSC composite source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef NTSC_COMP_SOURCE_STAGE_H
#define NTSC_COMP_SOURCE_STAGE_H

#include <stage_parameter.h>
#include <video_field_representation.h>

#include <string>

#include "../../../sdk/include/orc/plugin/orc_stage_preview.h"
#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"

namespace orc {

class INTSCCompSourceLoader {
 public:
  virtual ~INTSCCompSourceLoader() = default;

  virtual std::shared_ptr<VideoFieldRepresentation> load(
      const std::string& input_path, const std::string& db_path,
      const std::string& pcm_path, const std::string& efm_path,
      const std::string& ac3rf_path) const = 0;
};

/**
 * @brief NTSC Composite Source Stage - Loads NTSC TBC files from ld-decode
 *
 * This stage loads an NTSC TBC file and its associated database from ld-decode,
 * creating a VideoFieldRepresentation for NTSC video processing.
 *
 * Parameters:
 * - input_path: Path to the .tbc file
 * - db_path: Path to the .tbc.db database file (optional, defaults to
 * input_path + ".db")
 *
 * This is a source stage with no inputs.
 */
class NTSCCompSourceStage : public DAGStage,
                            public ParameterizedStage,
                            public PreviewableStage {
 public:
  explicit NTSCCompSourceStage(
      std::shared_ptr<INTSCCompSourceLoader> loader = nullptr);
  ~NTSCCompSourceStage() override = default;

  // DAGStage interface
  std::string version() const override { return "1.0.0"; }

  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{
        NodeType::SOURCE,
        "NTSC_Comp_Source",
        "NTSC Composite Source",
        "NTSC composite input source - loads NTSC TBC files from ld-decode",
        0,
        0,  // No inputs
        1,
        UINT32_MAX,  // Many outputs
        VideoFormatCompatibility::NTSC_ONLY,
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

  // Stage inspection
  std::optional<StageReport> generate_report() const override;

  // PreviewableStage interface
  bool supports_preview() const override { return true; }
  std::vector<PreviewOption> get_preview_options() const override;
  PreviewImage render_preview(const std::string& option_id, uint64_t index,
                              PreviewNavigationHint hint) const override;

 private:
  std::shared_ptr<VideoFieldRepresentation> load_representation(
      const std::string& input_path, const std::string& db_path,
      const std::string& pcm_path, const std::string& efm_path,
      const std::string& ac3rf_path) const;

  // Cache the loaded representation to avoid reloading
  mutable std::string cached_input_path_;
  mutable std::shared_ptr<VideoFieldRepresentation> cached_representation_;
  std::shared_ptr<INTSCCompSourceLoader> loader_;

  // Store parameters for inspection
  std::map<std::string, ParameterValue> parameters_;
};

}  // namespace orc

#endif  // NTSC_COMP_SOURCE_STAGE_H
