/*
 * File:        frame_phase_corrector_stage.h
 * Module:      orc-stage-plugin-frame-phase-corrector
 * Purpose:     Colour-frame sequence verification and field-swap correction
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cvbs_signal_constants.h>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../../../sdk/include/orc/plugin/orc_stage_preview.h"
#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "frame_descriptor.h"
#include "frame_id.h"
#include "stage_parameter.h"
#include "video_frame_representation.h"

namespace orc {

// ============================================================================
// FieldSwapCorrectedRepresentation
// ============================================================================
// VideoFrameRepresentationWrapper that swaps the two field blocks for frames
// where the TBC has stored the fields in the wrong temporal order.
//
// The correction is a pure index redirect — no sample data is copied.
// get_frame() returns nullptr because the corrected layout has no contiguous
// flat buffer in the source.  Consumers must use get_line().
//
// For frames that do not need swapping, all methods forward directly to the
// source without any indirection cost.
//
// Thread safety: all const methods are safe to call concurrently after
// construction.
class PhaseCorectedRepresentation : public VideoFrameRepresentationWrapper,
                                    public Artifact {
 public:
  // Per-frame correction record produced by FramePhaseCorrectorStage.
  struct FrameCorrection {
    bool swap_fields = false;
    int corrected_colour_index = -1;  // -1 = unknown / break detected
  };

  PhaseCorectedRepresentation(
      std::shared_ptr<const VideoFrameRepresentation> source,
      std::map<FrameID, FrameCorrection> corrections);

  ~PhaseCorectedRepresentation() override = default;

  std::string type_name() const override {
    return "phase_corrected_representation";
  }

  // Per-frame descriptor with corrected colour_frame_index.
  std::optional<FrameDescriptor> get_frame_descriptor(
      FrameID id) const override;

  // get_frame() returns nullptr when a field swap is active (no flat buffer).
  const sample_type* get_frame(FrameID id) const override;

  // Line access with field-block swap when needed.
  const sample_type* get_line(FrameID id, size_t line) const override;
  std::vector<sample_type> get_frame_copy(FrameID id) const override;

  // YC line access (same swap logic as composite)
  const sample_type* get_line_luma(FrameID id, size_t line) const override;
  const sample_type* get_line_chroma(FrameID id, size_t line) const override;

  // Phase hint follows the corrected colour_frame_index.
  std::optional<int> get_frame_phase_hint(FrameID id) const override;

 private:
  // Map of FrameID → correction; only entries that require action are stored.
  std::map<FrameID, FrameCorrection> corrections_;

  // Map output line → source line when field blocks are swapped.
  // field1_lines_ is set from the video system at construction time.
  size_t field1_lines_ = 0;

  size_t remap_line(size_t output_line, size_t frame_height) const;
};

// ============================================================================
// FramePhaseCorrectorStage
// ============================================================================
// Analyses the colour-frame-index sequence and:
//   1. Detects frames where the two field blocks are temporally swapped
//      (i.e., the TBC stored even field before odd).  When correct_field_swap
//      is true, presents such frames with the field blocks exchanged via an
//      index redirect — zero sample copies.
//   2. Walks the colour_frame_index sequence; marks frames at breaks with
//      colour_frame_index = -1 when verify_phase_sequence is true.
//
// Observations:
//   frame_phase_corrector.field_swaps_corrected  (int64)
//   frame_phase_corrector.phase_breaks_detected  (int64)
//   frame_phase_corrector.phase_breaks_marked    (int64)
class FramePhaseCorrectorStage : public DAGStage,
                                 public ParameterizedStage,
                                 public PreviewableStage {
 public:
  FramePhaseCorrectorStage() = default;

  std::string version() const override { return "1.0"; }

  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::TRANSFORM,
                        "frame_phase_corrector",
                        "Frame Phase Corrector",
                        "Correct field-swap errors and verify colour-frame "
                        "sequence integrity",
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

 private:
  bool correct_field_swap_ = true;
  bool verify_phase_sequence_ = true;

  mutable std::shared_ptr<const VideoFrameRepresentation> cached_output_;

  // Measure the burst phase of a half-frame (one field block) by sampling
  // the colour burst region.  Returns the mean burst phase angle in degrees,
  // or a sentinel value when the burst is absent.
  static double measure_field_burst_phase(const VideoFrameRepresentation& src,
                                          FrameID id, bool second_field,
                                          VideoSystem sys);

  // Determine whether the two field blocks in the frame appear to be swapped
  // based on burst phase progression.
  static bool detect_field_swap(const VideoFrameRepresentation& src, FrameID id,
                                VideoSystem sys);

  // Expected next colour_frame_index.
  static int next_colour_index(int current, VideoSystem sys);
};

}  // namespace orc
