/*
 * File:        frame_field_swap_stage.h
 * Module:      orc-stage-plugin-frame-field-swap
 * Purpose:     Frame field-block swap transform stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cvbs_signal_constants.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "artifact.h"
#include "dropout_util.h"
#include "preview_renderer.h"
#include "stage_parameter.h"
#include "video_frame_representation.h"

namespace orc {

// ============================================================================
// FrameFieldSwapRepresentation
// ============================================================================
// VFrameR wrapper that swaps the two field blocks in the flat frame buffer.
//
// Source layout: [field1_lines | field2_lines]
// Output layout: [field2_lines | field1_lines]
//
// Access is via get_line() only — get_frame() returns nullptr because no
// contiguous flat buffer for the swapped layout exists in the source.
// Dropout hints are remapped so consumers receive OUTPUT coordinates.
class FrameFieldSwapRepresentation : public VideoFrameRepresentationWrapper,
                                     public Artifact {
 public:
  explicit FrameFieldSwapRepresentation(
      std::shared_ptr<const VideoFrameRepresentation> source)
      : VideoFrameRepresentationWrapper(std::move(source)),
        Artifact(ArtifactID("frame_field_swap"), Provenance{}) {
    auto params = source_ ? source_->get_video_parameters() : std::nullopt;
    if (params.has_value()) {
      switch (params->system) {
        case VideoSystem::PAL:
          field1_lines_ = kPalField1Lines;
          break;
        case VideoSystem::NTSC:
          field1_lines_ = kNtscField1Lines;
          break;
        case VideoSystem::PAL_M:
          field1_lines_ = kPalMField1Lines;
          break;
        default:
          field1_lines_ = 0;
          break;
      }
    }
  }

  std::string type_name() const override {
    return "frame_field_swap_representation";
  }

  // No flat buffer — callers must use get_line().
  const sample_type* get_frame(FrameID /*id*/) const override {
    return nullptr;
  }

  // Return a line from the swapped layout.
  const sample_type* get_line(FrameID id, size_t line) const override;

  // Build a complete swapped frame copy.
  std::vector<sample_type> get_frame_copy(FrameID id) const override;

  // Remap dropout hint offsets from source to output coordinates.
  std::vector<DropoutRun> get_dropout_hints(FrameID id) const override;

  // YC channels follow the same line remapping.
  const sample_type* get_line_luma(FrameID id, size_t line) const override;
  const sample_type* get_line_chroma(FrameID id, size_t line) const override;

 private:
  size_t field1_lines_ = 0;

  // Map output line index to source line index.
  // output_line [0 .. field2_lines-1] → source [field1_lines .. H-1]
  // output_line [field2_lines .. H-1] → source [0 .. field1_lines-1]
  size_t remap_line(size_t output_line, size_t frame_height) const;
};

// ============================================================================
// FrameFieldSwapStage
// ============================================================================
class FrameFieldSwapStage : public DAGStage,
                            public ParameterizedStage,
                            public PreviewableStage {
 public:
  FrameFieldSwapStage() = default;

  std::string version() const override { return "1.0"; }
  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::TRANSFORM,
                        "frame_field_swap",
                        "Frame Field Swap",
                        "Swap the two interlaced field blocks in each frame",
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

  std::shared_ptr<const VideoFrameRepresentation> process(
      std::shared_ptr<const VideoFrameRepresentation> source) const;

  // ParameterizedStage
  std::vector<ParameterDescriptor> get_parameter_descriptors(
      VideoSystem project_format, SourceType source_type) const override;
  using ParameterizedStage::get_parameter_descriptors;
  std::map<std::string, ParameterValue> get_parameters() const override;
  bool set_parameters(
      const std::map<std::string, ParameterValue>& params) override;

  // PreviewableStage
  bool supports_preview() const override { return true; }
  std::vector<PreviewOption> get_preview_options() const override;
  PreviewImage render_preview(const std::string& option_id, uint64_t index,
                              PreviewNavigationHint hint) const override;

 private:
  mutable std::shared_ptr<const VideoFrameRepresentation> cached_output_;
};

}  // namespace orc
