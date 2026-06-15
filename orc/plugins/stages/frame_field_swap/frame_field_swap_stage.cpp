/*
 * File:        frame_field_swap_stage.cpp
 * Module:      orc-stage-plugin-frame-field-swap
 * Purpose:     Frame field-block swap transform stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "frame_field_swap_stage.h"

#include <algorithm>
#include <cstdint>

#include "error_types.h"
#include "frame_line_util.h"
#include "logging.h"
#include "preview_helpers.h"

namespace orc {

// ============================================================================
// FrameFieldSwapRepresentation
// ============================================================================

size_t FrameFieldSwapRepresentation::remap_line(size_t output_line,
                                                size_t frame_height) const {
  if (field1_lines_ == 0 || frame_height == 0) return output_line;
  const size_t field2_lines = frame_height - field1_lines_;
  if (output_line < field2_lines) {
    // First block of output: field2 data (source lines field1_lines..H-1)
    return field1_lines_ + output_line;
  }
  // Second block of output: field1 data (source lines 0..field1_lines-1)
  return output_line - field2_lines;
}

const FrameFieldSwapRepresentation::sample_type*
FrameFieldSwapRepresentation::get_line(FrameID id, size_t line) const {
  if (!source_) return nullptr;
  auto desc = source_->get_frame_descriptor(id);
  if (!desc.has_value()) return nullptr;
  return source_->get_line(id, remap_line(line, desc->height));
}

std::vector<FrameFieldSwapRepresentation::sample_type>
FrameFieldSwapRepresentation::get_frame_copy(FrameID id) const {
  if (!source_) return {};
  auto desc = source_->get_frame_descriptor(id);
  auto params = source_->get_video_parameters();
  if (!desc.has_value() || !params.has_value()) return {};

  const size_t height = desc->height;
  const int32_t nominal_spl = params->frame_width_nominal;
  std::vector<sample_type> result;
  result.reserve(desc->samples_total);

  for (size_t out_line = 0; out_line < height; ++out_line) {
    const size_t src_line = remap_line(out_line, height);
    const size_t width = frame_line_sample_count(
        params->system, static_cast<size_t>(nominal_spl), src_line);
    const sample_type* ptr = source_->get_line(id, src_line);
    if (ptr) {
      result.insert(result.end(), ptr, ptr + width);
    } else {
      result.insert(result.end(), width, sample_type{0});
    }
  }
  return result;
}

std::vector<DropoutRun> FrameFieldSwapRepresentation::get_dropout_hints(
    FrameID id) const {
  if (!source_) return {};
  auto runs = source_->get_dropout_hints(id);
  if (runs.empty()) return {};

  auto params = source_->get_video_parameters();
  auto desc = source_->get_frame_descriptor(id);
  if (!params.has_value() || !desc.has_value() || field1_lines_ == 0) {
    return runs;
  }

  const VideoSystem sys = params->system;
  const int32_t nominal_spl = params->frame_width_nominal;
  const size_t frame_height = desc->height;
  const size_t field2_lines = frame_height - field1_lines_;

  // Build output-frame cumulative line-offset table for flat-offset remapping.
  // output_offsets[i] = flat sample offset of output line i in the OUTPUT
  // frame.
  std::vector<uint64_t> out_offsets(frame_height + 1, 0);
  for (size_t out_line = 0; out_line < frame_height; ++out_line) {
    const size_t src_line = remap_line(out_line, frame_height);
    out_offsets[out_line + 1] =
        out_offsets[out_line] +
        frame_line_sample_count(sys, static_cast<size_t>(nominal_spl),
                                src_line);
  }

  std::vector<DropoutRun> result;
  result.reserve(runs.size());

  for (const auto& run : runs) {
    // Convert source flat offset → (field, line_in_field, sample_in_line).
    const auto fls =
        dropout_util::frame_sample_to_field_line(sys, run.sample_start);

    // Source frame line.
    const size_t src_frame_line =
        (fls.field == 1) ? static_cast<size_t>(fls.line)
                         : field1_lines_ + static_cast<size_t>(fls.line);

    // Map to output frame line (inverse of remap_line).
    size_t out_line;
    if (src_frame_line >= field1_lines_) {
      // Was field2 → now in first block of output.
      out_line = src_frame_line - field1_lines_;
    } else {
      // Was field1 → now in second block of output.
      out_line = field2_lines + src_frame_line;
    }
    if (out_line >= frame_height) {
      result.push_back(run);  // fallback: keep as-is
      continue;
    }

    const uint64_t new_start =
        out_offsets[out_line] + static_cast<uint64_t>(fls.sample);

    DropoutRun remapped = run;
    remapped.sample_start = new_start;
    result.push_back(remapped);
  }
  return result;
}

const FrameFieldSwapRepresentation::sample_type*
FrameFieldSwapRepresentation::get_line_luma(FrameID id, size_t line) const {
  if (!source_) return nullptr;
  auto desc = source_->get_frame_descriptor(id);
  if (!desc.has_value()) return nullptr;
  return source_->get_line_luma(id, remap_line(line, desc->height));
}

const FrameFieldSwapRepresentation::sample_type*
FrameFieldSwapRepresentation::get_line_chroma(FrameID id, size_t line) const {
  if (!source_) return nullptr;
  auto desc = source_->get_frame_descriptor(id);
  if (!desc.has_value()) return nullptr;
  return source_->get_line_chroma(id, remap_line(line, desc->height));
}

// ============================================================================
// FrameFieldSwapStage
// ============================================================================

std::vector<ArtifactPtr> FrameFieldSwapStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>&,
    ObservationContext& observation_context) {
  (void)observation_context;
  if (inputs.empty()) {
    throw DAGExecutionError("FrameFieldSwapStage requires one input");
  }
  auto input = inputs[0];
  if (!input) {
    throw DAGExecutionError("FrameFieldSwapStage received null input");
  }
  auto vfr = std::dynamic_pointer_cast<const VideoFrameRepresentation>(input);
  if (!vfr) {
    throw DAGExecutionError(
        "FrameFieldSwapStage input must be VideoFrameRepresentation");
  }
  auto output = process(vfr);
  cached_output_ = output;

  std::vector<ArtifactPtr> outputs;
  outputs.push_back(std::const_pointer_cast<FrameFieldSwapRepresentation>(
      std::dynamic_pointer_cast<const FrameFieldSwapRepresentation>(output)));
  return outputs;
}

std::shared_ptr<const VideoFrameRepresentation> FrameFieldSwapStage::process(
    std::shared_ptr<const VideoFrameRepresentation> source) const {
  if (!source) return nullptr;
  return std::make_shared<FrameFieldSwapRepresentation>(std::move(source));
}

std::vector<ParameterDescriptor> FrameFieldSwapStage::get_parameter_descriptors(
    VideoSystem, SourceType) const {
  return {};
}

std::map<std::string, ParameterValue> FrameFieldSwapStage::get_parameters()
    const {
  return {};
}

bool FrameFieldSwapStage::set_parameters(
    const std::map<std::string, ParameterValue>&) {
  return true;
}

std::vector<PreviewOption> FrameFieldSwapStage::get_preview_options() const {
  return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage FrameFieldSwapStage::render_preview(
    const std::string& option_id, uint64_t index,
    PreviewNavigationHint hint) const {
  return PreviewHelpers::render_standard_preview(cached_output_, option_id,
                                                 index, hint);
}

}  // namespace orc
