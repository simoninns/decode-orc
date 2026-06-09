/*
 * File:        audio_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Analogue Audio Sink Stage - writes PCM audio to WAV file
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "audio_sink_stage.h"

#include <common_types.h>

#include <stdexcept>

#include "audio_sink_stage_deps.h"
#include "audio_sink_stage_deps_interface.h"
#include "logging.h"

namespace orc {

AudioSinkStage::AudioSinkStage() = default;

NodeTypeInfo AudioSinkStage::get_node_type_info() const {
  return NodeTypeInfo{NodeType::SINK,
                      "AudioSink",
                      "Analogue Audio Sink",
                      "Extracts analogue audio PCM data and writes to WAV file",
                      1,
                      1,  // One input
                      0,
                      0,  // No outputs (sink)
                      VideoFormatCompatibility::ALL,
                      SinkCategory::CORE,
                      "Sink (Core)"};
}

std::vector<ArtifactPtr> AudioSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  // Sink stages don't produce outputs in execute()
  // Actual work happens in trigger()
  (void)inputs;
  (void)parameters;
  (void)observation_context;
  return {};
}

std::vector<ParameterDescriptor> AudioSinkStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType source_type) const {
  (void)project_format;
  (void)source_type;
  std::vector<ParameterDescriptor> descriptors;

  // output_path parameter
  {
    ParameterDescriptor desc;
    desc.name = "output_path";
    desc.display_name = "Output WAV File";
    desc.description = "Path to output WAV audio file";
    desc.type = ParameterType::FILE_PATH;
    desc.constraints.required = true;
    desc.constraints.default_value = std::string("");
    desc.file_extension_hint = ".wav";
    descriptors.push_back(desc);
  }

  return descriptors;
}

std::map<std::string, ParameterValue> AudioSinkStage::get_parameters() const {
  return parameters_;
}

bool AudioSinkStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  parameters_ = params;
  return true;
}

bool AudioSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context) {
  (void)observation_context;
  is_processing_.store(true);
  cancel_requested_.store(false);

  try {
    // Validate inputs
    if (inputs.empty()) {
      throw std::runtime_error(
          "Audio sink requires one input (VideoFieldRepresentation)");
    }

    auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
    if (!vfr) {
      throw std::runtime_error("Input must be a VideoFieldRepresentation");
    }

    // Check if VFR has audio
    if (!vfr->has_audio()) {
      throw std::runtime_error(
          "Input VFR does not have audio data (no PCM file specified in "
          "source?)");
    }

    // Get parameters
    auto output_path_it = parameters.find("output_path");
    if (output_path_it == parameters.end()) {
      throw std::runtime_error("output_path parameter is required");
    }
    if (!std::holds_alternative<std::string>(output_path_it->second)) {
      throw std::runtime_error("output_path parameter must be a string");
    }
    std::string output_path = std::get<std::string>(output_path_it->second);
    if (output_path.empty()) {
      throw std::runtime_error("output_path parameter is empty");
    }

    ORC_LOG_INFO("AudioSink: Writing audio to {}", output_path);

    std::shared_ptr<IAudioSinkStageDeps> deps = deps_override_;
    if (!deps) {
      auto deps_impl = std::make_shared<AudioSinkStageDeps>();
      deps_impl->init(progress_callback_, &is_processing_, &cancel_requested_);
      deps = deps_impl;
    }

    const auto write_result = deps->write_audio_wav(vfr.get(), output_path);
    if (!write_result.success) {
      last_status_ = "Error: " + write_result.error_message;
      ORC_LOG_ERROR("AudioSink: {}", write_result.error_message);
      is_processing_.store(false);
      return false;
    }

    ORC_LOG_INFO("AudioSink: Successfully wrote {} frames to {}",
                 write_result.frames_written, output_path);
    last_status_ = "Success: " + std::to_string(write_result.frames_written) +
                   " samples written";
    is_processing_.store(false);
    return true;

  } catch (const std::exception& e) {
    last_status_ = std::string("Error: ") + e.what();
    ORC_LOG_ERROR("AudioSink: {}", e.what());
    is_processing_.store(false);
    return false;
  }
}

std::string AudioSinkStage::get_trigger_status() const { return last_status_; }

}  // namespace orc
