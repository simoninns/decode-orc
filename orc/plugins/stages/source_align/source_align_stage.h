/*
 * File:        source_align_stage.h
 * Module:      orc-core
 * Purpose:     Source alignment stage for synchronizing multiple sources
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../../../sdk/include/orc/plugin/orc_stage_preview.h"
#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "stage_parameter.h"
#include "video_frame_representation.h"

namespace orc {

/**
 * @brief Source alignment stage that synchronizes multiple sources.
 *
 * Many-to-Many stage. Aligns multiple VFrameR inputs so that output
 * frame_id 0 represents the same content across all sources.
 * Auto-detects alignment from VBI data (get_vbi_hint) or accepts a manual
 * alignment map ("1+2, 2+2, 3+1, 4+1" = input_id+frame_offset).
 */
class SourceAlignStage : public DAGStage,
                         public ParameterizedStage,
                         public PreviewableStage {
 public:
  SourceAlignStage() = default;

  std::string version() const override { return "1.0"; }

  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::COMPLEX, "source_align", "Source Align",
                        "Synchronize multiple sources by VBI frame number",
                        1,
                        16,
                        2,
                        UINT32_MAX,
                        VideoFormatCompatibility::ALL,
                        SinkCategory::CORE,
                        "Transform"};
  }

  std::vector<ArtifactPtr> execute(
      const std::vector<ArtifactPtr>& inputs,
      const std::map<std::string, ParameterValue>& parameters,
      ObservationContext& observation_context) override;

  size_t required_input_count() const override { return 1; }
  size_t output_count() const override { return UINT32_MAX; }

  // PreviewableStage
  bool supports_preview() const override { return true; }
  std::vector<PreviewOption> get_preview_options() const override;
  PreviewImage render_preview(const std::string& option_id, uint64_t index,
                              PreviewNavigationHint hint) const override;

  // Stage inspection
  std::optional<StageReport> generate_report() const override;

  // ParameterizedStage
  std::vector<ParameterDescriptor> get_parameter_descriptors(
      VideoSystem project_format, SourceType source_type) const override;
  using ParameterizedStage::get_parameter_descriptors;
  std::map<std::string, ParameterValue> get_parameters() const override;
  bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

 private:
  std::vector<FrameID> find_alignment_offsets(
      const std::vector<std::shared_ptr<const VideoFrameRepresentation>>&
          sources) const;

  int32_t get_frame_number_from_vbi(const VideoFrameRepresentation& source,
                                    FrameID frame_id) const;

  static std::vector<std::pair<size_t, size_t>> parse_alignment_map(
      const std::string& alignment_spec);

  // Reporting state
  mutable std::vector<FrameID> alignment_offsets_;
  mutable std::vector<std::shared_ptr<const VideoFrameRepresentation>>
      input_sources_;

  // Preview state
  mutable std::vector<std::shared_ptr<const VideoFrameRepresentation>>
      cached_outputs_;

  // Parameters
  std::string alignment_map_;
};

}  // namespace orc
