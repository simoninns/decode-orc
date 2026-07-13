/*
 * File:        video_params_stage.h
 * Module:      orc-core
 * Purpose:     Video parameters override stage (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <orc/plugin/orc_stage_preview.h>
#include <orc/plugin/orc_stage_runtime.h>
#include <orc/stage/artifact.h>
#include <orc/stage/stage_parameter.h>
#include <orc/stage/video_frame_representation.h>

#include <memory>

namespace orc {

// ============================================================================
// VideoParamsOverrideFrameRepresentation
// ============================================================================
// Wraps a VideoFrameRepresentation and returns overridden SourceParameters.
// All sample access is forwarded unchanged.
//
// When black_level or white_level are overridden, sets has_nonstandard_values.
// Geometry overrides only relabel the active window; they never set
// active_area_cropping_applied because the sample buffer is not cropped.
class VideoParamsOverrideFrameRepresentation
    : public VideoFrameRepresentationWrapper,
      public Artifact {
 public:
  VideoParamsOverrideFrameRepresentation(
      std::shared_ptr<const VideoFrameRepresentation> source,
      const std::optional<SourceParameters>& override_params)
      : VideoFrameRepresentationWrapper(std::move(source)),
        Artifact(ArtifactID("video_params_override"), Provenance{}) {
    if (override_params.has_value()) {
      cached_params_ = override_params;
    } else if (source_) {
      cached_params_ = source_->get_video_parameters();
    }
  }

  std::string type_name() const override {
    return "video_params_override_frame_representation";
  }

  std::optional<SourceParameters> get_video_parameters() const override {
    return cached_params_;
  }

  // This stage modifies only reported video parameters, never sample data, so
  // line reads may forward to the wrapped input to preserve its
  // seek-one-line-from-disk fast path (important for analysis sinks scanning
  // whole recordings).
  std::vector<sample_type> get_line_samples(FrameID id,
                                            size_t line) const override {
    return source_ ? source_->get_line_samples(id, line)
                   : std::vector<sample_type>{};
  }
  const sample_type* get_line(FrameID id, size_t line) const override {
    return source_ ? source_->get_line(id, line) : nullptr;
  }
  const sample_type* get_line_luma(FrameID id, size_t line) const override {
    return source_ ? source_->get_line_luma(id, line) : nullptr;
  }
  const sample_type* get_line_chroma(FrameID id, size_t line) const override {
    return source_ ? source_->get_line_chroma(id, line) : nullptr;
  }

 private:
  std::optional<SourceParameters> cached_params_;
};

// ============================================================================
// VideoParamsStage
// ============================================================================
class VideoParamsStage : public DAGStage,
                         public ParameterizedStage,
                         public IStagePreviewCapability {
 public:
  VideoParamsStage();

  std::string version() const override { return "1.0"; }
  ORC_STAGE_INSTRUCTIONS_MD
  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::TRANSFORM,
                        "video_params",
                        "Video Parameters",
                        "Override video parameters (dimensions, signal "
                        "levels, active area geometry)",
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

 private:
  std::optional<SourceParameters> build_video_parameters(
      const std::optional<SourceParameters>& source_params) const;

  mutable std::shared_ptr<const VideoFrameRepresentation> cached_output_;

  // All optional; -1 means "inherit from source".
  int32_t active_video_start_ = -1;
  int32_t active_video_end_ = -1;
  int32_t first_active_frame_line_ = -1;
  int32_t last_active_frame_line_ = -1;
  int32_t white_level_ = -1;
  int32_t black_level_ = -1;
};

}  // namespace orc
