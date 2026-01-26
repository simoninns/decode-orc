/*
 * File:        field_corruption_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Presenter for Field Corruption Generator analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/field_corruption_presenter.h"
#include "../../core/include/project.h"
#include "../../core/include/dag_executor.h"
#include "../../core/include/video_field_representation.h"
#include "../../core/include/logging.h"
#include "../../core/analysis/analysis_registry.h"
#include "../../core/analysis/analysis_progress.h"
#include "../../core/stages/stage.h"
#include <algorithm>

namespace orc::presenters {

FieldCorruptionPresenter::FieldCorruptionPresenter(void* project_handle)
    : AnalysisToolPresenter(project_handle) {
}

std::string FieldCorruptionPresenter::toolId() const {
    return "field_corruption";
}

std::string FieldCorruptionPresenter::toolName() const {
    return "Field Corruption Generator";
}

uint64_t FieldCorruptionPresenter::getInputFieldCount(NodeID node_id) {
    // Get the DAG
    auto dag = getOrBuildDAG();
    if (!dag) {
        ORC_LOG_WARN("Field Corruption Presenter: Cannot build DAG");
        return 0;
    }
    
    // Check if node has input
    if (!hasNodeInput(node_id)) {
        ORC_LOG_WARN("Field Corruption Presenter: Node has no input");
        return 0;
    }
    
    // Get the first input node ID
    auto input_node_id = getFirstInputNodeId(node_id);
    if (!input_node_id.is_valid()) {
        ORC_LOG_WARN("Field Corruption Presenter: Invalid input node ID");
        return 0;
    }
    
    // Execute the input node
    auto artifacts = executeToNode(input_node_id);
    
    // Find VideoFieldRepresentation artifact
    for (const auto& artifact : artifacts) {
        // artifacts are opaque void* handles, need to cast
        auto source = std::static_pointer_cast<const orc::VideoFieldRepresentation>(artifact);
        if (source) {
            uint64_t field_count = source->field_count();
            ORC_LOG_DEBUG("Field Corruption Presenter: Got field count {} from input", field_count);
            return field_count;
        }
    }
    
    ORC_LOG_WARN("Field Corruption Presenter: No VideoFieldRepresentation artifact found");
    return 0;
}

int32_t FieldCorruptionPresenter::getExistingSeed(NodeID node_id) {
    auto dag_void = getOrBuildDAG();
    if (!dag_void) {
        return 0;
    }
    auto dag = std::static_pointer_cast<orc::DAG>(dag_void);
    
    const auto& dag_nodes = dag->nodes();
    auto node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
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

orc::AnalysisResult FieldCorruptionPresenter::runAnalysis(
    NodeID node_id,
    const std::map<std::string, orc::ParameterValue>& parameters,
    std::function<void(int, const std::string&)> progress_callback) {
    
    orc::AnalysisResult result;
    result.status = orc::AnalysisResult::Status::Failed;
    
    // Report progress
    if (progress_callback) {
        progress_callback(0, "Initializing corruption generator...");
    }
    
    // Get the analysis tool from registry
    auto tool = orc::AnalysisRegistry::instance().findById(toolId());
    if (!tool) {
        result.summary = "Field Corruption Generator tool not found in registry";
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
    
    // Validate the node exists and is a field_map stage
    const auto& dag_nodes = dag->nodes();
    auto node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
        [&node_id](const DAGNode& node) { return node.node_id == node_id; });
    
    if (node_it == dag_nodes.end()) {
        result.summary = "Node not found in DAG";
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }
    
    if (!node_it->stage || node_it->stage->get_node_type_info().stage_name != "field_map") {
        result.summary = "Field Corruption Generator only applies to field_map stages";
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }
    
    if (progress_callback) {
        progress_callback(10, "Determining input field count...");
    }
    
    // Get input field count
    uint64_t field_count = getInputFieldCount(node_id);
    if (field_count == 0) {
        result.summary = "Cannot determine input field count.\n\n"
                        "Field Corruption Generator requires a field-based input.\n"
                        "Ensure this field_map stage has an input connection.";
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }
    
    if (progress_callback) {
        progress_callback(20, "Preparing analysis context...");
    }
    
    // Get existing seed (if any)
    int32_t existing_seed = getExistingSeed(node_id);
    
    // Prepare analysis context
    orc::AnalysisContext ctx;
    ctx.source_type = orc::AnalysisSourceType::LaserDisc;
    ctx.node_id = node_id;
    ctx.dag = dag;
    ctx.parameters = parameters;
    
    if (progress_callback) {
        progress_callback(30, "Running field corruption analysis...");
    }
    
    // Create progress wrapper
    class ProgressWrapper : public orc::AnalysisProgress {
    public:
        ProgressWrapper(std::function<void(int, const std::string&)> callback, int base_progress)
            : callback_(callback), base_progress_(base_progress) {}
        
        void setProgress(int percentage) override {
            last_progress_ = percentage;
            if (callback_) {
                int mapped = base_progress_ + (percentage * (90 - base_progress_)) / 100;
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
        
        bool isCancelled() const override { return false; }
        
        void reportPartialResult(const orc::AnalysisResult::ResultItem&) override {
            // Not used for this presenter
        }
        
    private:
        std::function<void(int, const std::string&)> callback_;
        int base_progress_;
        int last_progress_ = 0;
        std::string status_;
    };
    
    ProgressWrapper progress(progress_callback, 30);
    
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

} // namespace orc::presenters
