/*
 * File:        dropout_analysis_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Dropout Analysis Sink Stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "dropout_analysis_sink_stage.h"

#include <memory>
#include <stdexcept>

#include "dropout_analysis_sink_deps.h"
#include "logging.h"
#include "preview_helpers.h"

namespace orc {

DropoutAnalysisSinkStage::DropoutAnalysisSinkStage() = default;

NodeTypeInfo DropoutAnalysisSinkStage::get_node_type_info() const {
  return NodeTypeInfo{NodeType::ANALYSIS_SINK,
                      "dropout_analysis_sink",
                      "Dropout Analysis Sink",
                      "Computes dropout statistics and optionally writes CSV. "
                      "Trigger to update dataset.",
                      1,  // min_inputs
                      1,  // max_inputs
                      0,  // min_outputs
                      0,  // max_outputs
                      VideoFormatCompatibility::ALL,
                      SinkCategory::ANALYSIS,
                      "Sink (Analysis)"};
}

std::vector<ArtifactPtr> DropoutAnalysisSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  (void)parameters;
  (void)observation_context;
  // Cache input for preview rendering (pass-through preview of upstream output)
  if (!inputs.empty()) {
    cached_input_ =
        std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
  }
  // Sink stages do not emit artifacts during execute(); trigger() performs the
  // work.
  return {};
}

std::vector<ParameterDescriptor>
DropoutAnalysisSinkStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType source_type) const {
  (void)project_format;
  (void)source_type;

  std::vector<ParameterDescriptor> descriptors;

  descriptors.push_back(ParameterDescriptor{
      "output_path", "CSV Output Path",
      "Destination CSV file for dropout metrics. Leave empty to skip file "
      "output.",
      ParameterType::FILE_PATH,
      ParameterConstraints{
          std::nullopt, std::nullopt, std::string(""), {}, false, std::nullopt},
      ".csv"});

  descriptors.push_back(ParameterDescriptor{
      "write_csv", "Write CSV",
      "Enable writing results to CSV at trigger time.", ParameterType::BOOL,
      ParameterConstraints{std::nullopt,
                           std::nullopt,
                           ParameterValue(false),
                           {},
                           false,
                           std::nullopt}});

  descriptors.push_back(
      ParameterDescriptor{"mode", "Analysis Mode",
                          "Choose full-field or visible-area dropout analysis.",
                          ParameterType::STRING,
                          ParameterConstraints{std::nullopt,
                                               std::nullopt,
                                               std::string("full"),
                                               {"full", "visible"},
                                               true,
                                               std::nullopt}});

  descriptors.push_back(
      ParameterDescriptor{"max_frames", "Max Frames",
                          "Deprecated: data is automatically binned to ~1000 "
                          "points based on total frames (0 = auto).",
                          ParameterType::UINT32,
                          ParameterConstraints{ParameterValue(0U),
                                               std::nullopt,
                                               ParameterValue(0U),
                                               {},
                                               false,
                                               std::nullopt}});

  return descriptors;
}

std::map<std::string, ParameterValue> DropoutAnalysisSinkStage::get_parameters()
    const {
  return parameters_;
}

bool DropoutAnalysisSinkStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  parameters_ = params;
  return true;
}

DropoutAnalysisSinkStage::ParsedConfig DropoutAnalysisSinkStage::parse_config(
    const std::map<std::string, ParameterValue>& parameters) const {
  ParsedConfig cfg;

  // output_path
  auto out_it = parameters.find("output_path");
  if (out_it != parameters.end() &&
      std::holds_alternative<std::string>(out_it->second)) {
    cfg.output_path = std::get<std::string>(out_it->second);
  }

  // write_csv
  auto csv_it = parameters.find("write_csv");
  if (csv_it != parameters.end() &&
      std::holds_alternative<bool>(csv_it->second)) {
    cfg.write_csv = std::get<bool>(csv_it->second);
  }

  // mode
  auto mode_it = parameters.find("mode");
  if (mode_it != parameters.end() &&
      std::holds_alternative<std::string>(mode_it->second)) {
    auto m = std::get<std::string>(mode_it->second);
    if (m == "visible") {
      cfg.mode = DropoutAnalysisMode::VISIBLE_AREA;
    } else {
      cfg.mode = DropoutAnalysisMode::FULL_FIELD;
}
  }

  // max_frames
  auto max_it = parameters.find("max_frames");
  if (max_it != parameters.end() &&
      std::holds_alternative<uint32_t>(max_it->second)) {
    cfg.max_frames = static_cast<size_t>(std::get<uint32_t>(max_it->second));
  }

  return cfg;
}

bool DropoutAnalysisSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context) {
  ORC_LOG_DEBUG("DropoutAnalysisSink: Trigger started");
  is_processing_.store(true);
  cancel_requested_.store(false);
  has_results_ = false;
  frame_stats_.clear();
  total_frames_ = 0;

  try {
    if (inputs.empty()) {
      throw std::runtime_error("No input connected");
    }

    auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
    if (!vfr) {
      throw std::runtime_error("Input is not a VideoFieldRepresentation");
    }

    ParsedConfig cfg = parse_config(parameters);
    last_mode_ = cfg.mode;

    std::shared_ptr<IDropoutAnalysisSinkStageDeps> deps = deps_override_;
    if (!deps) {
      deps = std::make_shared<DropoutAnalysisSinkStageDeps>();
    }

    deps->init(progress_callback_, &cancel_requested_);

    DropoutAnalysisComputeOptions compute_options;
    compute_options.output_path = cfg.output_path;
    compute_options.write_csv = cfg.write_csv;
    compute_options.mode = cfg.mode;
    compute_options.max_frames = cfg.max_frames;

    const DropoutAnalysisComputeResult compute_result =
        deps->compute_and_analyze(vfr.get(), observation_context,
                                  compute_options);

    if (!compute_result.success) {
      has_results_ = false;
      frame_stats_.clear();
      total_frames_ = 0;
      last_status_ = compute_result.message.empty()
                         ? "Error: Dropout analysis failed"
                         : (compute_result.message == "Cancelled by user"
                                ? compute_result.message
                                : "Error: " + compute_result.message);
      is_processing_.store(false);
      return false;
    }

    frame_stats_ = compute_result.frame_stats;
    total_frames_ = compute_result.total_frames;

    if (cfg.write_csv && !cfg.output_path.empty()) {
      if (!deps->write_csv(cfg.output_path, frame_stats_)) {
        ORC_LOG_WARN("DropoutAnalysisSink: Failed to write CSV to {}",
                     cfg.output_path);
      }
    }

    last_status_ = "Dropout analysis complete";
    has_results_ = true;
    is_processing_.store(false);
    return true;

  } catch (const std::exception& e) {
    last_status_ = std::string("Error: ") + e.what();
    ORC_LOG_ERROR("DropoutAnalysisSink: Trigger failed: {}", e.what());
    is_processing_.store(false);
    return false;
  }
}

std::vector<PreviewOption> DropoutAnalysisSinkStage::get_preview_options()
    const {
  return PreviewHelpers::get_standard_preview_options(cached_input_);
}

PreviewImage DropoutAnalysisSinkStage::render_preview(
    const std::string& option_id, uint64_t index,
    PreviewNavigationHint hint) const {
  (void)hint;
  return PreviewHelpers::render_standard_preview(cached_input_, option_id,
                                                 index);
}

}  // namespace orc
