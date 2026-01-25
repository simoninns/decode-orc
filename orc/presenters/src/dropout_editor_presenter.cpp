/*
 * File:        dropout_editor_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Presenter for Dropout Editor analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/dropout_editor_presenter.h"
#include "../../core/include/project.h"
#include "../../core/include/dag_executor.h"
#include "../../core/include/logging.h"
#include "../../core/analysis/analysis_tool.h"
#include "../../core/analysis/dropout/dropout_editor_tool.h"
#include "../../core/analysis/analysis_progress.h"
#include "../../core/analysis/analysis_context.h"
#include "../../core/stages/stage.h"
#include <algorithm>
#include <atomic>

namespace orc::presenters {

DropoutEditorPresenter::DropoutEditorPresenter(void* project_handle)
    : AnalysisToolPresenter(project_handle) {
    ORC_LOG_DEBUG("DropoutEditorPresenter created");
}

orc::public_api::AnalysisResult DropoutEditorPresenter::runAnalysis(
    NodeID node_id,
    const std::map<std::string, orc::ParameterValue>& parameters,
    std::function<void(int, const std::string&)> progress_callback) {
    
    (void)parameters;  // Dropout editor has no parameters
    
    ORC_LOG_DEBUG("DropoutEditorPresenter::runAnalysis for node {}", node_id.value());
    
    // Report initial progress
    if (progress_callback) {
        progress_callback(0, "Preparing dropout editor...");
    }
    
    // Build DAG to validate node
    auto dag_void = getOrBuildDAG();
    if (!dag_void) {
        ORC_LOG_ERROR("Failed to build DAG");
        orc::public_api::AnalysisResult result;
        result.status = orc::public_api::AnalysisResult::Status::Failed;
        result.summary = "Failed to build project DAG";
        return result;
    }
    auto dag = std::static_pointer_cast<orc::DAG>(dag_void);
    
    // Validate node exists and is a dropout_map stage
    const auto& nodes = dag->nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [node_id](const orc::DAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        ORC_LOG_ERROR("Node {} not found", node_id.value());
        orc::public_api::AnalysisResult result;
        result.status = orc::public_api::AnalysisResult::Status::Failed;
        result.summary = "Node not found in project";
        return result;
    }
    
    if (!node_it->stage || node_it->stage->get_node_type_info().stage_name != "dropout_map") {
        ORC_LOG_ERROR("Node {} is not a dropout_map stage", node_id.value());
        orc::public_api::AnalysisResult result;
        result.status = orc::public_api::AnalysisResult::Status::Failed;
        result.summary = "This tool only works with dropout_map stages";
        return result;
    }
    
    // Report additional progress
    if (progress_callback) {
        progress_callback(20, "Validating input connections...");
    }
    
    // Check if node has input (needed to get field data)
    if (!hasNodeInput(node_id)) {
        ORC_LOG_ERROR("Node {} has no input connection", node_id.value());
        orc::public_api::AnalysisResult result;
        result.status = orc::public_api::AnalysisResult::Status::Failed;
        result.summary = "Dropout map stage must have an input connection to provide field data";
        return result;
    }
    
    // Execute input node to get field artifacts
    if (progress_callback) {
        progress_callback(40, "Executing input node to get field data...");
    }
    
    auto input_node_id = getFirstInputNodeId(node_id);
    std::vector<std::shared_ptr<void>> input_artifacts_void;
    try {
        input_artifacts_void = executeToNode(input_node_id);
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to execute input node: {}", e.what());
        orc::public_api::AnalysisResult result;
        result.status = orc::public_api::AnalysisResult::Status::Failed;
        result.summary = std::string("Failed to execute input node: ") + e.what();
        return result;
    }
    
    // Cast opaque handles back to concrete artifacts
    std::vector<std::shared_ptr<const orc::Artifact>> input_artifacts;
    for (const auto& void_artifact : input_artifacts_void) {
        input_artifacts.push_back(std::static_pointer_cast<const orc::Artifact>(void_artifact));
    }
    
    if (input_artifacts.empty()) {
        ORC_LOG_ERROR("Input node produced no artifacts");
        orc::public_api::AnalysisResult result;
        result.status = orc::public_api::AnalysisResult::Status::Failed;
        result.summary = "Input node produced no field data";
        return result;
    }
    
    // Create analysis context with DAG
    orc::AnalysisContext ctx;
    ctx.dag = dag;
    ctx.node_id = node_id;
    // Note: ctx.project not set (would require shared_ptr)
    
    // Create progress adapter
    class ProgressAdapter : public orc::AnalysisProgress {
    public:
        explicit ProgressAdapter(std::function<void(int, const std::string&)> callback)
            : callback_(callback), cancelled_(false), current_percentage_(0) {}
        
        void setProgress(int percentage) override {
            current_percentage_ = percentage;
            if (callback_) {
                callback_(percentage, current_status_);
            }
        }
        
        void setStatus(const std::string& status) override {
            current_status_ = status;
            if (callback_) {
                callback_(current_percentage_, status);
            }
        }
        
        void setSubStatus(const std::string& message) override {
            // Not mapped separately; reuse status channel
            setStatus(message);
        }
        
        bool isCancelled() const override {
            return cancelled_;
        }
        
        void reportPartialResult(const orc::AnalysisResult::ResultItem&) override {
            // Not used for dropout editor
        }
        
    private:
        std::function<void(int, const std::string&)> callback_;
        std::atomic<bool> cancelled_;
        int current_percentage_;
        std::string current_status_;
    };
    
    ProgressAdapter progress_adapter(progress_callback);
    
    // Call the core tool
    if (progress_callback) {
        progress_callback(60, "Launching dropout editor...");
    }
    
    orc::DropoutEditorTool tool;
    orc::AnalysisResult core_result = tool.analyze(ctx, &progress_adapter);
    
    // Convert core result to public API result
    orc::public_api::AnalysisResult result;
    
    switch (core_result.status) {
        case orc::AnalysisResult::Success:
            result.status = orc::public_api::AnalysisResult::Status::Success;
            break;
        case orc::AnalysisResult::Failed:
            result.status = orc::public_api::AnalysisResult::Status::Failed;
            break;
        case orc::AnalysisResult::Cancelled:
            result.status = orc::public_api::AnalysisResult::Status::Cancelled;
            break;
    }
    
    result.summary = core_result.summary;
    
    // The dropout editor is a GUI tool - the actual editing happens in the dialog
    // which is launched separately. This just validates the setup.
    
    if (progress_callback) {
        progress_callback(100, "Dropout editor ready");
    }
    
    ORC_LOG_DEBUG("DropoutEditorPresenter::runAnalysis completed with status: {}", 
                 static_cast<int>(result.status));
    
    return result;
}

} // namespace orc::presenters
