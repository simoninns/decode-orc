/*
 * File:        dropout_map_stage.h
 * Module:      orc-core
 * Purpose:     Dropout map stage — override dropout hints per frame (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../../sdk/include/orc/plugin/orc_stage_preview.h"
#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "../../../sdk/include/orc/plugin/orc_stage_tooling.h"
#include "artifact.h"
#include "dropout_run.h"
#include "dropout_util.h"
#include "stage_parameter.h"
#include "video_frame_representation.h"

namespace orc {

// ============================================================================
// FrameDropoutMapEntry
// ============================================================================
// Per-frame dropout additions and removals.
// Coordinates are in frame-flat 0-based line / sample-within-line form so that
// apply-time conversion to DropoutRun is straightforward.
struct DropoutEntrySpec {
  uint32_t line;          // Frame-flat 0-based line
  uint32_t start_sample;  // Sample-within-line (inclusive)
  uint32_t end_sample;    // Sample-within-line (inclusive)
};

struct FrameDropoutMapEntry {
  FrameID frame_id = 0;
  std::vector<DropoutEntrySpec> additions;
  std::vector<DropoutEntrySpec> removals;
};

// ============================================================================
// DropoutMappedFrameRepresentation
// ============================================================================
class DropoutMappedFrameRepresentation : public VideoFrameRepresentationWrapper,
                                         public Artifact {
 public:
  DropoutMappedFrameRepresentation(
      std::shared_ptr<const VideoFrameRepresentation> source,
      const std::map<uint64_t, FrameDropoutMapEntry>& dropout_map);

  std::string type_name() const override {
    return "dropout_mapped_frame_representation";
  }

  std::vector<DropoutRun> get_dropout_hints(FrameID id) const override;

 private:
  std::map<uint64_t, FrameDropoutMapEntry> dropout_map_;

  // Convert (frame_id, line, sample_start, sample_end) to a DropoutRun.
  static std::optional<DropoutRun> entry_to_run(
      VideoSystem sys, int32_t nominal_spl, FrameID frame_id,
      const DropoutEntrySpec& entry);
};

// ============================================================================
// DropoutMapStage
// ============================================================================
class DropoutMapStage : public DAGStage,
                        public ParameterizedStage,
                        public PreviewableStage,
                        public StageToolProvider {
 public:
  DropoutMapStage() = default;

  std::string version() const override { return "1.0"; }

  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{
        NodeType::TRANSFORM,
        "dropout_map",
        "Dropout Map",
        "Override dropout hints on per-frame basis — add, remove, or modify "
        "dropout regions",
        1,
        1,
        1,
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
  size_t output_count() const override { return 1; }

  // PreviewableStage
  bool supports_preview() const override { return true; }
  std::vector<PreviewOption> get_preview_options() const override;
  PreviewImage render_preview(const std::string& option_id, uint64_t index,
                              PreviewNavigationHint hint) const override;

  // ParameterizedStage
  std::vector<ParameterDescriptor> get_parameter_descriptors(
      VideoSystem project_format, SourceType source_type) const override;
  using ParameterizedStage::get_parameter_descriptors;
  std::map<std::string, ParameterValue> get_parameters() const override;
  bool set_parameters(
      const std::map<std::string, ParameterValue>& params) override;

  // Parse/encode helpers (public for GUI dropout editor)
  static std::map<uint64_t, FrameDropoutMapEntry> parse_dropout_map(
      const std::string& map_str);
  static std::string encode_dropout_map(
      const std::map<uint64_t, FrameDropoutMapEntry>& map);

  std::vector<StageToolDescriptor> get_stage_tools() const override {
    return {StageToolDescriptor{"dropout_editor", "Dropout Editor",
                                "Open interactive dropout map editor",
                                StageToolKind::NonModalEditor, true,
                                "decode-orc.stage-tools.dropout-editor.v1"}};
  }

 private:
  std::string dropout_map_str_ = "[]";
  mutable std::shared_ptr<const VideoFrameRepresentation> cached_output_;
};

}  // namespace orc
