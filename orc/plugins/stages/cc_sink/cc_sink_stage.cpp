/*
 * File:        cc_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Closed Caption Sink Stage - exports CC data to SCC or plain text
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "cc_sink_stage.h"

#include <memory>
#include <stdexcept>

#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "cc_sink_stage_deps.h"
#include "logging.h"

namespace orc {

CCSinkStage::CCSinkStage() = default;

NodeTypeInfo CCSinkStage::get_node_type_info() const {
  return NodeTypeInfo{NodeType::SINK,
                      "CCSink",
                      "Closed Caption Sink",
                      "Exports closed caption data to SCC or plain text format",
                      1,
                      1,  // One input
                      0,
                      0,  // No outputs (sink)
                      VideoFormatCompatibility::ALL,
                      SinkCategory::CORE,
                      "Sink (Core)"};
}

std::vector<ArtifactPtr> CCSinkStage::execute(
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

std::vector<ParameterDescriptor> CCSinkStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType source_type) const {
  (void)project_format;
  (void)source_type;
  std::vector<ParameterDescriptor> descriptors;

  // output_path parameter
  {
    ParameterDescriptor desc;
    desc.name = "output_path";
    desc.display_name = "Output File";
    desc.description = "Path to output closed caption file";
    desc.type = ParameterType::FILE_PATH;
    desc.constraints.required = true;
    desc.constraints.default_value = std::string("");
    descriptors.push_back(desc);
  }

  // format parameter
  {
    ParameterDescriptor desc;
    desc.name = "format";
    desc.display_name = "Export Format";
    desc.description = "Output format: Scenarist SCC V1.0 or plain text";
    desc.type = ParameterType::STRING;
    desc.constraints.required = true;
    desc.constraints.allowed_strings = {"Scenarist SCC", "Plain Text"};
    desc.constraints.default_value = std::string("Scenarist SCC");
    descriptors.push_back(desc);
  }

  return descriptors;
}

std::map<std::string, ParameterValue> CCSinkStage::get_parameters() const {
  return parameters_;
}

bool CCSinkStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  parameters_ = params;
  return true;
}

CCSinkStage::ParsedConfig CCSinkStage::parse_config(
    const std::map<std::string, ParameterValue>& parameters) const {
  ParsedConfig cfg;

  auto output_path_it = parameters.find("output_path");
  if (output_path_it == parameters.end() ||
      !std::holds_alternative<std::string>(output_path_it->second)) {
    throw std::runtime_error("output_path parameter is required");
  }
  cfg.output_path = std::get<std::string>(output_path_it->second);

  auto format_it = parameters.find("format");
  if (format_it != parameters.end() &&
      std::holds_alternative<std::string>(format_it->second)) {
    std::string format_str = std::get<std::string>(format_it->second);
    if (format_str == "Plain Text") {
      cfg.format = CCExportFormat::PLAIN_TEXT;
    }
  }

  auto write_csv_it = parameters.find("write_csv");
  if (write_csv_it != parameters.end() &&
      std::holds_alternative<bool>(write_csv_it->second)) {
    cfg.write_csv = std::get<bool>(write_csv_it->second);
  }

  return cfg;
}

bool CCSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context) {
  is_processing_.store(true);
  cancel_requested_.store(false);

  try {
    // Validate inputs
    if (inputs.empty()) {
      throw std::runtime_error(
          "CC sink requires one input (VideoFieldRepresentation)");
    }

    auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
    if (!vfr) {
      throw std::runtime_error("Input must be a VideoFieldRepresentation");
    }

    ParsedConfig cfg = parse_config(parameters);

    std::shared_ptr<ICCSinkStageDeps> deps = deps_override_;
    if (!deps) {
      deps = std::make_shared<CCSinkStageDeps>();
    }

    deps->init(progress_callback_, &cancel_requested_);

    CCExportOptions options;
    options.output_path = cfg.output_path;
    options.export_format = cfg.format;
    options.write_csv = cfg.write_csv;

    const CCExportResult export_result =
        deps->export_cc(vfr.get(), observation_context, options);

    is_processing_.store(false);

    if (!export_result.success) {
      ORC_LOG_ERROR("CC sink error: {}",
                    export_result.message.empty()
                        ? "Failed to export closed captions"
                        : export_result.message);
      return false;
    }

    ORC_LOG_INFO("Closed caption export completed successfully: {} CC frames",
                 export_result.cc_frames_exported);
    return true;

  } catch (const std::exception& e) {
    ORC_LOG_ERROR("CC sink error: {}", e.what());
    is_processing_.store(false);
    return false;
  }
}

std::string CCSinkStage::get_trigger_status() const {
  if (is_processing_.load()) {
    return "Exporting closed captions...";
  }
  return "Idle";
}

}  // namespace orc
