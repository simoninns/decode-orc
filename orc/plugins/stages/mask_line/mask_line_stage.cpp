/*
 * File:        mask_line_stage.cpp
 * Module:      orc-core
 * Purpose:     Line masking stage (VFrameR, frame-flat line addressing)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "mask_line_stage.h"

#include <cvbs_signal_constants.h>

#include <algorithm>
#include <sstream>

#include "error_types.h"
#include "frame_line_util.h"
#include "logging.h"
#include "preview_helpers.h"

namespace orc {

// ============================================================================
// MaskedFrameRepresentation
// ============================================================================

MaskedFrameRepresentation::MaskedFrameRepresentation(
    std::shared_ptr<const VideoFrameRepresentation> source,
    const std::string& line_spec, int32_t mask_sample_level)
    : VideoFrameRepresentationWrapper(std::move(source)),
      Artifact(ArtifactID("masked_frame"), Provenance{}),
      mask_sample_level_(mask_sample_level) {
  parse_line_spec(line_spec);
}

void MaskedFrameRepresentation::parse_line_spec(const std::string& spec) {
  line_ranges_.clear();
  if (spec.empty()) return;

  std::istringstream ss(spec);
  std::string token;
  while (std::getline(ss, token, ',')) {
    token.erase(0, token.find_first_not_of(" \t"));
    const size_t last = token.find_last_not_of(" \t");
    if (last == std::string::npos) continue;
    token = token.substr(0, last + 1);

    const size_t dash = token.find('-');
    if (dash != std::string::npos) {
      try {
        const size_t start = std::stoul(token.substr(0, dash));
        const size_t end = std::stoul(token.substr(dash + 1));
        line_ranges_.push_back({start, end});
      } catch (...) {
        ORC_LOG_WARN("MaskLine: invalid range '{}'", token);
      }
    } else {
      try {
        const size_t line = std::stoul(token);
        line_ranges_.push_back({line, line});
      } catch (...) {
        ORC_LOG_WARN("MaskLine: invalid line '{}'", token);
      }
    }
  }
}

bool MaskedFrameRepresentation::should_mask_line(size_t frame_line) const {
  for (const auto& r : line_ranges_) {
    if (frame_line >= r.start && frame_line <= r.end) return true;
  }
  return false;
}

const MaskedFrameRepresentation::sample_type*
MaskedFrameRepresentation::get_line(FrameID id, size_t line) const {
  if (!source_) return nullptr;
  if (!should_mask_line(line)) return source_->get_line(id, line);

  auto params = source_ ? source_->get_video_parameters() : std::nullopt;
  if (!params.has_value()) return nullptr;
  const size_t width = frame_line_sample_count(
      params->system, static_cast<size_t>(params->frame_width_nominal), line);

  const int16_t val =
      static_cast<int16_t>(std::clamp(mask_sample_level_, 0, 1023));
  masked_line_buffer_.assign(width, val);
  return masked_line_buffer_.data();
}

std::vector<MaskedFrameRepresentation::sample_type>
MaskedFrameRepresentation::get_frame_copy(FrameID id) const {
  if (!source_) return {};
  auto desc = source_->get_frame_descriptor(id);
  auto params = source_->get_video_parameters();
  if (!desc || !params) return {};

  std::vector<sample_type> result;
  result.reserve(desc->samples_total);
  for (size_t line = 0; line < desc->height; ++line) {
    const size_t w = frame_line_sample_count(
        params->system, static_cast<size_t>(params->frame_width_nominal), line);
    const sample_type* ptr = get_line(id, line);
    if (ptr) {
      result.insert(result.end(), ptr, ptr + w);
    } else {
      result.insert(result.end(), w, sample_type{0});
    }
  }
  return result;
}

const MaskedFrameRepresentation::sample_type*
MaskedFrameRepresentation::get_line_luma(FrameID id, size_t line) const {
  if (!source_ || !source_->has_separate_channels()) return nullptr;
  if (!should_mask_line(line)) return source_->get_line_luma(id, line);

  auto params_luma = source_ ? source_->get_video_parameters() : std::nullopt;
  if (!params_luma.has_value()) return nullptr;
  const size_t width = frame_line_sample_count(
      params_luma->system,
      static_cast<size_t>(params_luma->frame_width_nominal), line);
  const int16_t val =
      static_cast<int16_t>(std::clamp(mask_sample_level_, 0, 1023));
  masked_luma_buffer_.assign(width, val);
  return masked_luma_buffer_.data();
}

const MaskedFrameRepresentation::sample_type*
MaskedFrameRepresentation::get_line_chroma(FrameID id, size_t line) const {
  if (!source_ || !source_->has_separate_channels()) return nullptr;
  if (!should_mask_line(line)) return source_->get_line_chroma(id, line);

  auto params_chroma = source_ ? source_->get_video_parameters() : std::nullopt;
  if (!params_chroma.has_value()) return nullptr;
  const size_t width = frame_line_sample_count(
      params_chroma->system,
      static_cast<size_t>(params_chroma->frame_width_nominal), line);
  const int16_t val =
      static_cast<int16_t>(std::clamp(mask_sample_level_, 0, 1023));
  masked_chroma_buffer_.assign(width, val);
  return masked_chroma_buffer_.data();
}

// ============================================================================
// MaskLineStage
// ============================================================================

MaskLineStage::MaskLineStage() {
  set_configuration_status(orc::ConfigurationStatus::Yellow);
}

std::vector<ArtifactPtr> MaskLineStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  (void)observation_context;
  if (inputs.empty()) {
    throw DAGExecutionError("MaskLineStage requires one input");
  }
  auto input = inputs[0];
  if (!input) {
    throw DAGExecutionError("MaskLineStage received null input");
  }

  auto vfr = std::dynamic_pointer_cast<const VideoFrameRepresentation>(input);
  if (!vfr) {
    throw DAGExecutionError(
        "MaskLineStage input must be VideoFrameRepresentation");
  }

  if (!parameters.empty()) {
    const_cast<MaskLineStage*>(this)->set_parameters(parameters);
  }

  auto output = process(vfr);
  cached_output_ = output;

  std::vector<ArtifactPtr> outputs;
  outputs.push_back(std::const_pointer_cast<MaskedFrameRepresentation>(
      std::dynamic_pointer_cast<const MaskedFrameRepresentation>(output)));
  return outputs;
}

std::shared_ptr<const VideoFrameRepresentation> MaskLineStage::process(
    std::shared_ptr<const VideoFrameRepresentation> source) const {
  if (!source) return nullptr;
  if (line_spec_.empty()) return source;  // pass-through
  return std::make_shared<MaskedFrameRepresentation>(source, line_spec_,
                                                     mask_sample_level_);
}

std::vector<ParameterDescriptor> MaskLineStage::get_parameter_descriptors(
    VideoSystem, SourceType) const {
  return {
      ParameterDescriptor{
          "lineSpec", "Line Specification",
          "Frame-flat 0-based line numbers to mask. Format: LINE or START-END, "
          "comma-separated. Examples: '21' (NTSC CC line), '100-115', "
          "'21,334'.",
          ParameterType::STRING,
          ParameterConstraints{std::nullopt,
                               std::nullopt,
                               ParameterValue{std::string("")},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "maskSampleLevel", "Mask Sample Level",
          "10-bit sample level for masked pixels (0=sync tip, 256=blanking, "
          "844=white for PAL/NTSC).",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{int32_t{0}},
                               ParameterValue{int32_t{1023}},
                               ParameterValue{int32_t{0}},
                               {},
                               false,
                               std::nullopt}}};
}

std::map<std::string, ParameterValue> MaskLineStage::get_parameters() const {
  return {{"lineSpec", line_spec_}, {"maskSampleLevel", mask_sample_level_}};
}

bool MaskLineStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  for (const auto& [key, value] : params) {
    if (key == "lineSpec" && std::holds_alternative<std::string>(value)) {
      line_spec_ = std::get<std::string>(value);
    } else if (key == "maskSampleLevel" &&
               std::holds_alternative<int32_t>(value)) {
      mask_sample_level_ =
          std::clamp(std::get<int32_t>(value), int32_t{0}, int32_t{1023});
    }
  }
  set_configuration_status(line_spec_.empty()
                               ? orc::ConfigurationStatus::Yellow
                               : orc::ConfigurationStatus::Green);
  return true;
}

StagePreviewCapability MaskLineStage::get_preview_capability() const {
  return PreviewHelpers::make_signal_preview_capability(cached_output_);
}

}  // namespace orc
