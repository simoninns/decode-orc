/*
 * File:        mask_line_stage.h
 * Module:      orc-core
 * Purpose:     Line masking stage (VFrameR, frame-flat line addressing)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "../../../sdk/include/orc/plugin/orc_stage_tooling.h"
#include "artifact.h"
#include "preview_renderer.h"
#include "stage_parameter.h"
#include "video_frame_representation.h"

namespace orc {

// ============================================================================
// MaskedFrameRepresentation
// ============================================================================
// Wraps a VideoFrameRepresentation and blanks specified frame-flat lines.
//
// Line spec format: comma-separated line numbers or inclusive ranges.
//   "100"        – mask frame line 100
//   "100-115"    – mask frame lines 100 through 115 (inclusive)
//   "21,334-336" – mask line 21 and lines 334-336
//
// Mask value is in IRE units (0-100) converted to CVBS_U10_4FSC int16_t using
// blanking_level and white_level from the source SourceParameters.
class MaskedFrameRepresentation : public VideoFrameRepresentationWrapper,
                                  public Artifact {
 public:
  MaskedFrameRepresentation(
      std::shared_ptr<const VideoFrameRepresentation> source,
      const std::string& line_spec, double mask_ire);

  std::string type_name() const override {
    return "masked_frame_representation";
  }

  const sample_type* get_line(FrameID id, size_t line) const override;
  std::vector<sample_type> get_frame_copy(FrameID id) const override;

  const sample_type* get_line_luma(FrameID id, size_t line) const override;
  const sample_type* get_line_chroma(FrameID id, size_t line) const override;

 private:
  struct LineRange {
    size_t start;
    size_t end;
  };

  bool should_mask_line(size_t frame_line) const;
  void parse_line_spec(const std::string& line_spec);
  int16_t ire_to_sample(double ire) const;

  std::vector<LineRange> line_ranges_;
  double mask_ire_;

  mutable std::vector<int16_t> masked_line_buffer_;
  mutable std::vector<int16_t> masked_luma_buffer_;
  mutable std::vector<int16_t> masked_chroma_buffer_;
};

// ============================================================================
// MaskLineStage
// ============================================================================
class MaskLineStage : public DAGStage,
                      public ParameterizedStage,
                      public IStagePreviewCapability,
                      public StageToolProvider {
 public:
  MaskLineStage();

  std::string version() const override { return "1.0"; }
  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::TRANSFORM,
                        "mask_line",
                        "Mask Line",
                        "Mask (blank) specified frame-flat lines",
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

  // IStagePreviewCapability
  StagePreviewCapability get_preview_capability() const override;

  std::vector<StageToolDescriptor> get_stage_tools() const override {
    return {StageToolDescriptor{"mask_line_config", "Mask Line Config",
                                "Open mask-line helper dialog for line presets",
                                StageToolKind::ConfigDialog, false,
                                "decode-orc.stage-tools.mask-line-config.v1"}};
  }

 private:
  std::string line_spec_ = "";
  double mask_ire_ = 0.0;

  mutable std::shared_ptr<const VideoFrameRepresentation> cached_output_;
};

}  // namespace orc
