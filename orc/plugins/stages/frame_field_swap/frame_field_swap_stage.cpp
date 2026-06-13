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
#include "logging.h"

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

/*static*/ size_t FrameFieldSwapRepresentation::line_sample_width(
    VideoSystem sys, size_t src_line, int32_t nominal_spl) {
  if (sys != VideoSystem::PAL) return static_cast<size_t>(nominal_spl);
  // EBU Tech. 3280-E §1.3.1: four lines carry one extra sample.
  const bool extra = (src_line == static_cast<size_t>(kPalExtraSampleLines[0]) ||
                      src_line == static_cast<size_t>(kPalExtraSampleLines[1]) ||
                      src_line == static_cast<size_t>(kPalExtraSampleLines[2]) ||
                      src_line == static_cast<size_t>(kPalExtraSampleLines[3]));
  return extra ? static_cast<size_t>(nominal_spl + 1)
               : static_cast<size_t>(nominal_spl);
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
    const size_t width = line_sample_width(params->system, src_line, nominal_spl);
    const sample_type* ptr = source_->get_line(id, src_line);
    if (ptr) {
      result.insert(result.end(), ptr, ptr + width);
    } else {
      result.insert(result.end(), width, sample_type{0});
    }
  }
  return result;
}

std::vector<DropoutRun>
FrameFieldSwapRepresentation::get_dropout_hints(FrameID id) const {
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
  // output_offsets[i] = flat sample offset of output line i in the OUTPUT frame.
  std::vector<uint64_t> out_offsets(frame_height + 1, 0);
  for (size_t out_line = 0; out_line < frame_height; ++out_line) {
    const size_t src_line = remap_line(out_line, frame_height);
    out_offsets[out_line + 1] =
        out_offsets[out_line] + line_sample_width(sys, src_line, nominal_spl);
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

namespace {

// Render a VFrameR frame as grayscale (shared helper, mirrors cvbs_source).
PreviewImage render_vfr_grayscale(const VideoFrameRepresentation& vfr,
                                  FrameID frame_id, bool apply_scaling) {
  auto desc = vfr.get_frame_descriptor(frame_id);
  auto params = vfr.get_video_parameters();
  if (!desc || !params) return PreviewImage{0, 0, {}, {}, {}};

  const size_t height = desc->height;
  const size_t width = static_cast<size_t>(params->frame_width_nominal);
  if (height == 0 || width == 0) return PreviewImage{0, 0, {}, {}, {}};

  const int32_t blanking = params->blanking_level;
  const int32_t white = params->white_level;
  const int32_t range = (white > blanking) ? (white - blanking) : 1;

  PreviewImage img;
  img.width = static_cast<uint32_t>(width);
  img.height = static_cast<uint32_t>(height);
  img.rgb_data.reserve(width * height * 3);

  for (size_t line = 0; line < height; ++line) {
    const int16_t* ptr = vfr.get_line(frame_id, line);
    for (size_t s = 0; s < width; ++s) {
      const int32_t raw = ptr ? static_cast<int32_t>(ptr[s]) : blanking;
      uint8_t grey;
      if (apply_scaling) {
        grey = static_cast<uint8_t>(
            std::clamp((raw - blanking) * 255 / range, 0, 255));
      } else {
        grey = static_cast<uint8_t>(std::clamp(raw * 255 / 1023, 0, 255));
      }
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
    }
  }
  return img;
}

}  // namespace

std::vector<PreviewOption> FrameFieldSwapStage::get_preview_options() const {
  if (!cached_output_) return {};

  auto params = cached_output_->get_video_parameters();
  const size_t fc = cached_output_->frame_count();
  if (fc == 0 || !params.has_value()) return {};

  const uint32_t w = static_cast<uint32_t>(params->frame_width_nominal);
  const uint32_t h = static_cast<uint32_t>(params->frame_height);

  double dar = 0.7;
  if (params->active_video_start >= 0 &&
      params->active_video_end > params->active_video_start &&
      params->first_active_frame_line >= 0 &&
      params->last_active_frame_line > params->first_active_frame_line) {
    const double aw = static_cast<double>(params->active_video_end -
                                          params->active_video_start);
    const double ah = static_cast<double>(params->last_active_frame_line -
                                          params->first_active_frame_line);
    dar = (4.0 / 3.0) / (aw / ah);
  }

  return {
      PreviewOption{"frame", "Frame (Scaled)", false, w, h,
                    static_cast<uint64_t>(fc), dar},
      PreviewOption{"frame_raw", "Frame (Raw)", false, w, h,
                    static_cast<uint64_t>(fc), dar},
  };
}

PreviewImage FrameFieldSwapStage::render_preview(
    const std::string& option_id, uint64_t index,
    PreviewNavigationHint /*hint*/) const {
  if (!cached_output_) return PreviewImage{0, 0, {}, {}, {}};
  const FrameID fid = static_cast<FrameID>(index);
  if (!cached_output_->has_frame(fid)) return PreviewImage{0, 0, {}, {}, {}};
  const bool scale = (option_id != "frame_raw");
  return render_vfr_grayscale(*cached_output_, fid, scale);
}

}  // namespace orc
