/*
 * File:        dropout_correct_stage.h
 * Module:      orc-core
 * Purpose:     Dropout correction stage (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "../../../sdk/include/orc/plugin/orc_stage_preview.h"
#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "artifact.h"
#include "dropout_decision.h"
#include "frame_descriptor.h"
#include "lru_cache.h"
#include "stage_parameter.h"
#include "video_frame_representation.h"

namespace orc {

struct DropoutCorrectionConfig {
  uint32_t overcorrect_extension = 0;
  bool intrafield_only = false;
  uint32_t max_replacement_distance = 10;
  bool match_chroma_phase = true;
  bool highlight_corrections = false;
};

// Forward declaration
class DropoutCorrectStage;

// ============================================================================
// Internal per-line dropout descriptor (frame-flat addressing)
// ============================================================================
struct LineDropout {
  uint32_t line;
  uint32_t start_sample;
  uint32_t end_sample;
};

// ============================================================================
// CorrectedVideoFrameRepresentation
// ============================================================================
class CorrectedVideoFrameRepresentation : public VideoFrameRepresentationWrapper,
                                          public Artifact {
 public:
  CorrectedVideoFrameRepresentation(
      std::shared_ptr<const VideoFrameRepresentation> source,
      DropoutCorrectStage* stage, bool highlight_corrections);

  std::string type_name() const override {
    return "corrected_video_frame_representation";
  }

  const int16_t* get_line(FrameID id, size_t line) const override;
  std::vector<int16_t> get_frame_copy(FrameID id) const override;

  const int16_t* get_line_luma(FrameID id, size_t line) const override;
  const int16_t* get_line_chroma(FrameID id, size_t line) const override;

  // After correction there are no dropouts in the output.
  std::vector<DropoutRun> get_dropout_hints(FrameID /*id*/) const override {
    return {};
  }

  friend class DropoutCorrectStage;

 private:
  DropoutCorrectStage* stage_;
  bool highlight_corrections_;

  static constexpr size_t MAX_CACHED_FRAMES = 150;

  mutable LRUCache<FrameID, std::vector<int16_t>> corrected_frames_;
  mutable LRUCache<FrameID, std::vector<int16_t>> corrected_luma_frames_;
  mutable LRUCache<FrameID, std::vector<int16_t>> corrected_chroma_frames_;

  void ensure_frame_corrected(FrameID frame_id) const;
};

// ============================================================================
// DropoutCorrectStage
// ============================================================================
class DropoutCorrectStage : public DAGStage,
                            public ParameterizedStage,
                            public PreviewableStage {
 public:
  explicit DropoutCorrectStage(
      const DropoutCorrectionConfig& config = DropoutCorrectionConfig{})
      : config_(config) {}

  std::string version() const override { return "1.0"; }

  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::TRANSFORM,
                        "dropout_correct",
                        "Dropout Correction",
                        "Correct dropouts by replacing corrupted samples with "
                        "data from other lines",
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
  bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

  // Public for lazy correction from CorrectedVideoFrameRepresentation
  void correct_single_frame(CorrectedVideoFrameRepresentation* corrected,
                            std::shared_ptr<const VideoFrameRepresentation> source,
                            FrameID frame_id) const;

  // Public for unit testing
  static std::vector<LineDropout> runs_to_line_dropouts(
      const std::vector<DropoutRun>& runs, size_t nominal_spl);

 private:
  DropoutCorrectionConfig config_;

  enum class DropoutLocation { COLOUR_BURST, VISIBLE_LINE, UNKNOWN };
  enum class Channel { COMPOSITE, LUMA, CHROMA };

  DropoutLocation classify_dropout(const LineDropout& dropout,
                                   const FrameDescriptor& desc,
                                   const std::optional<SourceParameters>& params) const;

  std::vector<LineDropout> split_dropout_regions(
      const std::vector<LineDropout>& dropouts,
      const FrameDescriptor& desc,
      const std::optional<SourceParameters>& params) const;

  struct ReplacementLine {
    bool found = false;
    FrameID source_frame = 0;
    uint32_t source_line = 0;
    double quality = 0.0;
    uint32_t distance = 0;
    const int16_t* cached_data = nullptr;
  };

  ReplacementLine find_replacement_line(
      const VideoFrameRepresentation& source, FrameID frame_id, uint32_t line,
      const LineDropout& dropout, bool intrafield, bool match_chroma_phase_override,
      size_t field1_lines, Channel channel = Channel::COMPOSITE) const;

  void apply_correction(std::vector<int16_t>& line_data,
                        const LineDropout& dropout,
                        const int16_t* replacement_data,
                        int16_t highlight_value, bool highlight) const;

  double calculate_line_quality(const int16_t* line_data, size_t width,
                                const LineDropout& dropout) const;

  mutable std::shared_ptr<const VideoFrameRepresentation> cached_output_;
};

}  // namespace orc
