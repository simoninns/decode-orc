/*
 * File:        video_params_stage.cpp
 * Module:      orc-core
 * Purpose:     Video parameters override stage (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "video_params_stage.h"

#include <algorithm>

#include "error_types.h"
#include "logging.h"
#include "preview_helpers.h"

namespace orc {

// ============================================================================
// VideoParamsStage
// ============================================================================

VideoParamsStage::VideoParamsStage() {
  set_configuration_status(orc::ConfigurationStatus::Yellow);
}

std::vector<ArtifactPtr> VideoParamsStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  (void)observation_context;
  if (inputs.empty()) {
    throw DAGExecutionError("VideoParamsStage requires one input");
  }
  auto input = inputs[0];
  if (!input) throw DAGExecutionError("VideoParamsStage received null input");

  auto vfr = std::dynamic_pointer_cast<const VideoFrameRepresentation>(input);
  if (!vfr) {
    throw DAGExecutionError(
        "VideoParamsStage input must be VideoFrameRepresentation");
  }

  if (!parameters.empty()) set_parameters(parameters);

  auto output = process(vfr);
  cached_output_ = output;
  ORC_LOG_DEBUG("VideoParamsStage::execute - cached_output_ = {}",
                static_cast<const void*>(output.get()));

  std::vector<ArtifactPtr> outputs;
  outputs.push_back(
      std::const_pointer_cast<VideoParamsOverrideFrameRepresentation>(
          std::dynamic_pointer_cast<
              const VideoParamsOverrideFrameRepresentation>(output)));
  return outputs;
}

std::shared_ptr<const VideoFrameRepresentation> VideoParamsStage::process(
    std::shared_ptr<const VideoFrameRepresentation> source) const {
  if (!source) return nullptr;
  auto override_params = build_video_parameters(source->get_video_parameters());
  return std::make_shared<VideoParamsOverrideFrameRepresentation>(
      std::move(source), override_params);
}

std::optional<SourceParameters> VideoParamsStage::build_video_parameters(
    const std::optional<SourceParameters>& source_params) const {
  SourceParameters params;
  if (source_params.has_value()) params = *source_params;

  bool level_overridden = false;
  bool geometry_overridden = false;

  if (active_video_start_ >= 0) {
    params.active_video_start = active_video_start_;
    geometry_overridden = true;
  }
  if (active_video_end_ >= 0) {
    params.active_video_end = active_video_end_;
    geometry_overridden = true;
  }
  if (first_active_frame_line_ >= 0) {
    params.first_active_frame_line = first_active_frame_line_;
    geometry_overridden = true;
  }
  if (last_active_frame_line_ >= 0) {
    params.last_active_frame_line = last_active_frame_line_;
    geometry_overridden = true;
  }
  if (white_level_ >= 0) {
    params.white_level = white_level_;
    level_overridden = true;
  }
  if (black_level_ >= 0) {
    params.black_level = black_level_;
    level_overridden = true;
  }

  if (level_overridden) params.has_nonstandard_values = true;
  if (geometry_overridden) params.active_area_cropping_applied = true;

  return params;
}

std::vector<ParameterDescriptor> VideoParamsStage::get_parameter_descriptors(
    VideoSystem, SourceType) const {
  return {
      ParameterDescriptor{
          "activeVideoStart", "Active Video Start",
          "Override active video start sample (0-based within line). "
          "-1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(10000)},
                               ParameterValue{static_cast<int32_t>(-1)},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "activeVideoEnd", "Active Video End",
          "Override active video end sample (0-based within line). "
          "-1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(10000)},
                               ParameterValue{static_cast<int32_t>(-1)},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "firstActiveFrameLine", "First Active Frame Line",
          "Override first active frame line (0-based, frame-flat). "
          "PAL: ~44, NTSC: ~40. -1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(1200)},
                               ParameterValue{static_cast<int32_t>(-1)},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "lastActiveFrameLine", "Last Active Frame Line",
          "Override last active frame line (0-based, frame-flat). "
          "PAL: ~619, NTSC: ~519. -1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(1200)},
                               ParameterValue{static_cast<int32_t>(-1)},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "whiteLevel", "White Level (10-bit)",
          "Override white level in CVBS_U10_4FSC domain (0-1023). "
          "PAL: 844, NTSC: 800. -1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(1023)},
                               ParameterValue{static_cast<int32_t>(-1)},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "blackLevel", "Black Level (10-bit)",
          "Override black level in CVBS_U10_4FSC domain (0-1023). "
          "PAL: 282, NTSC: 282. -1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(1023)},
                               ParameterValue{static_cast<int32_t>(-1)},
                               {},
                               false,
                               std::nullopt}},
  };
}

std::map<std::string, ParameterValue> VideoParamsStage::get_parameters() const {
  return {{"activeVideoStart", ParameterValue{active_video_start_}},
          {"activeVideoEnd", ParameterValue{active_video_end_}},
          {"firstActiveFrameLine", ParameterValue{first_active_frame_line_}},
          {"lastActiveFrameLine", ParameterValue{last_active_frame_line_}},
          {"whiteLevel", ParameterValue{white_level_}},
          {"blackLevel", ParameterValue{black_level_}}};
}

bool VideoParamsStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  for (const auto& [key, value] : params) {
    const auto* v = std::get_if<int32_t>(&value);
    if (!v) {
      return false;
    }
    if (key == "activeVideoStart") {
      active_video_start_ = *v;
    } else if (key == "activeVideoEnd") {
      active_video_end_ = *v;
    } else if (key == "firstActiveFrameLine") {
      first_active_frame_line_ = *v;
    } else if (key == "lastActiveFrameLine") {
      last_active_frame_line_ = *v;
    } else if (key == "whiteLevel") {
      white_level_ = *v;
    } else if (key == "blackLevel") {
      black_level_ = *v;
    } else {
      return false;
    }
  }
  const bool any_set = active_video_start_ != -1 || active_video_end_ != -1 ||
                       first_active_frame_line_ != -1 ||
                       last_active_frame_line_ != -1 || white_level_ != -1 ||
                       black_level_ != -1;
  set_configuration_status(any_set ? orc::ConfigurationStatus::Green
                                   : orc::ConfigurationStatus::Yellow);
  return true;
}

StagePreviewCapability VideoParamsStage::get_preview_capability() const {
  return PreviewHelpers::make_signal_preview_capability(cached_output_);
}

}  // namespace orc
