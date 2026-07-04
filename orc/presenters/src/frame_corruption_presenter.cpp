/*
 * File:        frame_corruption_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Presenter for Frame Corruption Generator analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/frame_corruption_presenter.h"

#include <orc/stage/logging.h>
#include <orc/stage/stage.h>
#include <orc/stage/video_frame_representation.h>

#include <algorithm>

#include "../../core/analysis/analysis_progress.h"
#include "../../core/analysis/analysis_registry.h"
#include "../../core/include/dag_executor.h"
#include "../../core/include/project.h"

namespace orc::presenters {

FrameCorruptionPresenter::FrameCorruptionPresenter(void* project_handle)
    : AnalysisToolPresenter(project_handle) {}

std::string FrameCorruptionPresenter::toolId() const {
  return "frame_corruption";
}

std::string FrameCorruptionPresenter::toolName() const {
  return "Frame Corruption Generator";
}

uint64_t FrameCorruptionPresenter::getInputFrameCount(NodeID node_id) {
  // Get the DAG
  auto dag = getOrBuildDAG();
  if (!dag) {
    ORC_LOG_WARN("Frame Corruption Presenter: Cannot build DAG");
    return 0;
  }

  // Check if node has input
  if (!hasNodeInput(node_id)) {
    ORC_LOG_WARN("Frame Corruption Presenter: Node has no input");
    return 0;
  }

  // Get the first input node ID
  auto input_node_id = getFirstInputNodeId(node_id);
  if (!input_node_id.is_valid()) {
    ORC_LOG_WARN("Frame Corruption Presenter: Invalid input node ID");
    return 0;
  }

  // Execute the input node
  auto artifacts = executeToNode(input_node_id);

  // Find VideoFrameRepresentation artifact
  for (const auto& artifact : artifacts) {
    // Cast void* back to Artifact base, then dynamic_cast to VFR
    auto artifact_base = std::static_pointer_cast<orc::Artifact>(artifact);
    auto vfr = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
        artifact_base);
    if (vfr) {
      uint64_t frame_count = vfr->frame_count();
      ORC_LOG_DEBUG("Frame Corruption Presenter: Got frame count {} from input",
                    frame_count);
      return frame_count;
    }
  }

  ORC_LOG_WARN(
      "Frame Corruption Presenter: No VideoFrameRepresentation artifact found");
  return 0;
}

int32_t FrameCorruptionPresenter::getExistingSeed(NodeID node_id) {
  auto dag_void = getOrBuildDAG();
  if (!dag_void) {
    return 0;
  }
  auto dag = std::static_pointer_cast<orc::DAG>(dag_void);

  const auto& dag_nodes = dag->nodes();
  auto node_it = std::find_if(
      dag_nodes.begin(), dag_nodes.end(),
      [&node_id](const DAGNode& node) { return node.node_id == node_id; });

  if (node_it != dag_nodes.end()) {
    auto seed_it = node_it->parameters.find("seed");
    if (seed_it != node_it->parameters.end()) {
      if (auto* val = std::get_if<int32_t>(&seed_it->second)) {
        return *val;
      }
    }
  }

  return 0;
}

orc::AnalysisResult FrameCorruptionPresenter::runAnalysis(
    NodeID node_id,
    const std::map<std::string, orc::ParameterValue>& parameters,
    std::function<void(int, const std::string&)> progress_callback,
    std::function<bool()> cancel_check) {
  orc::AnalysisResult result;
  result.status = orc::AnalysisResult::Status::Failed;

  // Report progress
  if (progress_callback) {
    progress_callback(0, "Initializing corruption generator...");
  }

  // Get the analysis tool from registry
  auto tool = orc::AnalysisRegistry::instance().findById(toolId());
  if (!tool) {
    result.summary = "Frame Corruption Generator tool not found in registry";
    ORC_LOG_ERROR("{}", result.summary);
    return result;
  }

  // Build DAG
  auto dag_void = getOrBuildDAG();
  if (!dag_void) {
    result.summary = "Failed to build DAG from project";
    ORC_LOG_ERROR("{}", result.summary);
    return result;
  }
  auto dag = std::static_pointer_cast<orc::DAG>(dag_void);

  // Validate the node exists and is a frame_map stage
  const auto& dag_nodes = dag->nodes();
  auto node_it = std::find_if(
      dag_nodes.begin(), dag_nodes.end(),
      [&node_id](const DAGNode& node) { return node.node_id == node_id; });

  if (node_it == dag_nodes.end()) {
    result.summary = "Node not found in DAG";
    ORC_LOG_ERROR("{}", result.summary);
    return result;
  }

  if (!node_it->stage ||
      node_it->stage->get_node_type_info().stage_name != "frame_map") {
    result.summary =
        "Frame Corruption Generator only applies to frame_map stages";
    ORC_LOG_ERROR("{}", result.summary);
    return result;
  }

  if (progress_callback) {
    progress_callback(10, "Determining input frame count...");
  }

  // Get input frame count
  uint64_t frame_count = getInputFrameCount(node_id);
  if (frame_count == 0) {
    result.summary =
        "Cannot determine input frame count.\n\n"
        "Frame Corruption Generator requires a VideoFrameRepresentation "
        "input.\n"
        "Ensure this frame_map stage has an input connection.";
    ORC_LOG_ERROR("{}", result.summary);
    return result;
  }

  if (progress_callback) {
    progress_callback(20, "Preparing analysis context...");
  }

  // Get existing seed (if any)
  int32_t existing_seed = getExistingSeed(node_id);
  (void)existing_seed;

  // Prepare analysis context
  orc::AnalysisContext ctx;
  ctx.source_type = orc::AnalysisSourceType::LaserDisc;
  ctx.node_id = node_id;
  ctx.dag = dag;
  ctx.parameters = parameters;

  if (progress_callback) {
    progress_callback(30, "Running frame corruption analysis...");
  }

  // Create progress wrapper
  class ProgressWrapper : public orc::AnalysisProgress {
   public:
    ProgressWrapper(std::function<void(int, const std::string&)> callback,
                    std::function<bool()> cancel_check, int base_progress)
        : callback_(callback),
          cancel_check_(std::move(cancel_check)),
          base_progress_(base_progress) {}

    void setProgress(int percentage) override {
      last_progress_ = percentage;
      if (callback_) {
        int mapped =
            base_progress_ + (percentage * (90 - base_progress_)) / 100;
        callback_(mapped, status_);
      }
    }

    void setStatus(const std::string& status) override {
      status_ = status;
      if (callback_) {
        callback_(base_progress_, status_);
      }
    }

    void setSubStatus(const std::string& message) override {
      // Not mapped separately; reuse status channel
      setStatus(message);
    }

    bool isCancelled() const override {
      return cancel_check_ && cancel_check_();
    }

    void reportPartialResult(const orc::AnalysisResult::ResultItem&) override {
      // Not used for this presenter
    }

   private:
    std::function<void(int, const std::string&)> callback_;
    std::function<bool()> cancel_check_;
    int base_progress_;
    int last_progress_ = 0;
    std::string status_;
  };

  ProgressWrapper progress(progress_callback, cancel_check, 30);

  // Run the analysis tool
  orc::AnalysisResult core_result = tool->analyze(ctx, &progress);

  if (progress_callback) {
    progress_callback(95, "Formatting results...");
  }

  // Convert core result to public API result
  result.status = core_result.status == orc::AnalysisResult::Success
                      ? orc::AnalysisResult::Status::Success
                      : orc::AnalysisResult::Status::Failed;
  result.summary = core_result.summary;
  result.graphData = core_result.graphData;

  if (progress_callback) {
    if (result.status == orc::AnalysisResult::Status::Success) {
      progress_callback(100, "Analysis complete");
    } else {
      progress_callback(0, "Analysis failed");
    }
  }

  return result;
}

}  // namespace orc::presenters
