/*
 * File:        video_params_stage.cpp
 * Module:      orc-core
 * Purpose:     Video parameters override stage (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "video_params_stage.h"

#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/error_types.h>
#include <orc/stage/logging.h>
#include <orc/stage/preview_helpers.h>

#include <algorithm>

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

  // Geometry overrides only relabel the active window; they must NOT touch
  // active_area_cropping_applied.  That flag means "the sample buffer is
  // already cropped to the active area, index it from 0" — but this stage
  // forwards samples unchanged, so downstream must keep using these offsets
  // against the full-frame buffer.  Leave the flag inherited from the source.
  if (active_video_start_ >= 0) {
    params.active_video_start = active_video_start_;
  }
  if (active_video_end_ >= 0) {
    params.active_video_end = active_video_end_;
  }
  if (first_active_frame_line_ >= 0) {
    params.first_active_frame_line = first_active_frame_line_;
  }
  if (last_active_frame_line_ >= 0) {
    params.last_active_frame_line = last_active_frame_line_;
  }
  if (white_level_ >= 0) {
    params.white_level = white_level_;
    level_overridden = true;
  }
  if (black_level_ >= 0) {
    params.black_level = black_level_;
    level_overridden = true;
  }

  // Flag non-standard levels by comparing the resulting values against the
  // system's spec constants — NOT merely because a level was specified.  This
  // lets the levels default to their spec values (see
  // get_parameter_descriptors) without falsely marking a standard source as
  // non-standard; only a value that actually differs from spec sets the flag.
  if (level_overridden) {
    int32_t spec_white = -1;
    int32_t spec_black = -1;
    switch (params.system) {
      case VideoSystem::PAL:
        spec_white = kPalWhite;
        spec_black = kPalBlack;
        break;
      case VideoSystem::NTSC:
      case VideoSystem::PAL_M:
        spec_white = kNtscWhite;
        spec_black = kNtscBlack;
        break;
      default:
        break;
    }
    if (params.white_level != spec_white || params.black_level != spec_black) {
      params.has_nonstandard_values = true;
    }
  }

  return params;
}

std::vector<ParameterDescriptor> VideoParamsStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType) const {
  // Present every parameter with the format's standard metadata value rather
  // than the bare -1 "inherit" sentinel, so they all read as ordinary populated
  // parameters.  For a standard source these equal the values that would be
  // inherited, so applying them is a no-op (and, for the levels, leaves
  // has_nonstandard_values clear — see build_video_parameters).  -1 remains a
  // valid explicit "inherit from source" value.  PAL_M shares NTSC geometry and
  // levels; Unknown keeps -1 since the standard values can't be known.
  int32_t av_start_default = -1;
  int32_t av_end_default = -1;
  int32_t first_line_default = -1;
  int32_t last_line_default = -1;
  int32_t white_default = -1;
  int32_t black_default = -1;
  switch (project_format) {
    case VideoSystem::PAL:
      av_start_default = kPalActiveVideoStart;
      av_end_default = kPalActiveVideoEnd;
      first_line_default = kPalFirstActiveFrameLine;
      last_line_default = kPalLastActiveFrameLine;
      white_default = kPalWhite;
      black_default = kPalBlack;
      break;
    case VideoSystem::NTSC:
    case VideoSystem::PAL_M:
      av_start_default = kNtscActiveVideoStart;
      av_end_default = kNtscActiveVideoEnd;
      first_line_default = kNtscFirstActiveFrameLine;
      last_line_default = kNtscLastActiveFrameLine;
      white_default = kNtscWhite;
      black_default = kNtscBlack;
      break;
    default:
      break;
  }
  return {
      ParameterDescriptor{
          "activeVideoStart", "Active Video Start",
          "Active video start sample (0-based within line). "
          "Defaults to the format standard (PAL: 157, NTSC: 126); "
          "-1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(10000)},
                               ParameterValue{av_start_default},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "activeVideoEnd", "Active Video End",
          "Active video end sample (0-based within line, exclusive). "
          "Defaults to the format standard (PAL: 1105, NTSC: 894); "
          "-1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(10000)},
                               ParameterValue{av_end_default},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "firstActiveFrameLine", "First Active Frame Line",
          "First active frame line (0-based, frame-flat). "
          "Defaults to the format standard (PAL: 44, NTSC: 40); "
          "-1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(1200)},
                               ParameterValue{first_line_default},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "lastActiveFrameLine", "Last Active Frame Line",
          "Last active frame line (0-based, frame-flat, exclusive). "
          "Defaults to the format standard (PAL: 620, NTSC: 523); "
          "-1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(1200)},
                               ParameterValue{last_line_default},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "whiteLevel", "White Level (10-bit)",
          "White level in CVBS_U10_4FSC domain (0-1023). "
          "Defaults to the format standard (PAL: 844, NTSC: 800); "
          "-1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(1023)},
                               ParameterValue{white_default},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "blackLevel", "Black Level (10-bit)",
          "Black level in CVBS_U10_4FSC domain (0-1023). "
          "Defaults to the format standard (PAL: 256, NTSC: 282); "
          "-1 = inherit from source.",
          ParameterType::INT32,
          ParameterConstraints{ParameterValue{static_cast<int32_t>(-1)},
                               ParameterValue{static_cast<int32_t>(1023)},
                               ParameterValue{black_default},
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
  auto capability =
      PreviewHelpers::make_signal_preview_capability(cached_output_);
  // Always show the full frame (blanking/VBI included) at its normal size and
  // aspect, with the region excluded by the active-area parameters dimmed.  The
  // preview never crops or rescales — only the actual video sink crops its
  // output — so the un-dimmed area shows exactly what the export will contain.
  capability.geometry.mask_inactive_area = true;
  return capability;
}

}  // namespace orc
