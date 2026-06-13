/*
 * File:        cvbs_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     CVBS Sink Stage - writes raw CVBS_U10_4FSC frames to file
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "cvbs_sink_stage.h"

#include <common_types.h>

#include <stdexcept>

#include "cvbs_sink_stage_deps.h"
#include "cvbs_sink_stage_deps_interface.h"
#include "logging.h"

namespace orc {

CVBSSinkStage::CVBSSinkStage() = default;

NodeTypeInfo CVBSSinkStage::get_node_type_info() const {
  return NodeTypeInfo{
      NodeType::SINK,
      "CVBSSink",
      "CVBS Data Sink",
      "Writes raw CVBS_U10_4FSC frame data (int16_t samples) to a binary file",
      1,
      1,
      0,
      0,
      VideoFormatCompatibility::ALL,
      SinkCategory::CORE,
      "Sink (Core)"};
}

std::vector<ArtifactPtr> CVBSSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  (void)inputs;
  (void)parameters;
  (void)observation_context;
  return {};
}

std::vector<ParameterDescriptor> CVBSSinkStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType source_type) const {
  (void)project_format;
  (void)source_type;
  std::vector<ParameterDescriptor> descriptors;

  {
    ParameterDescriptor desc;
    desc.name = "output_path";
    desc.display_name = "Output CVBS File";
    desc.description =
        "Path to output CVBS data file (raw int16_t samples, frame-flat)";
    desc.type = ParameterType::FILE_PATH;
    desc.constraints.required = true;
    desc.constraints.default_value = std::string("");
    desc.file_extension_hint = ".cvbs";
    descriptors.push_back(desc);
  }

  return descriptors;
}

std::map<std::string, ParameterValue> CVBSSinkStage::get_parameters() const {
  return parameters_;
}

bool CVBSSinkStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  parameters_ = params;
  return true;
}

bool CVBSSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context) {
  (void)observation_context;
  is_processing_.store(true);
  cancel_requested_.store(false);

  try {
    if (inputs.empty()) {
      throw std::runtime_error(
          "CVBS sink requires one input (VideoFrameRepresentation)");
    }

    auto vfr = std::dynamic_pointer_cast<VideoFrameRepresentation>(inputs[0]);
    if (!vfr) {
      throw std::runtime_error("Input must be a VideoFrameRepresentation");
    }

    auto output_path_it = parameters.find("output_path");
    if (output_path_it == parameters.end()) {
      throw std::runtime_error("output_path parameter is required");
    }
    const std::string output_path =
        std::get<std::string>(output_path_it->second);

    ORC_LOG_INFO("CVBSSink: Writing CVBS data to {}", output_path);

    std::shared_ptr<ICVBSSinkStageDeps> deps = deps_override_;
    if (!deps) {
      auto deps_impl = std::make_shared<CVBSSinkStageDeps>();
      deps_impl->init(progress_callback_, &cancel_requested_);
      deps = deps_impl;
    }

    const auto result = deps->write_cvbs(vfr.get(), output_path);
    last_status_ = result.status_message;

    if (!result.success) {
      ORC_LOG_ERROR("CVBSSink: {}", result.status_message);
      is_processing_.store(false);
      return false;
    }

    ORC_LOG_INFO("CVBSSink: {}", result.status_message);
    is_processing_.store(false);
    return true;

  } catch (const std::exception& e) {
    last_status_ = std::string("Error: ") + e.what();
    ORC_LOG_ERROR("CVBSSink: {}", e.what());
    is_processing_.store(false);
    return false;
  }
}

std::string CVBSSinkStage::get_trigger_status() const { return last_status_; }

}  // namespace orc
