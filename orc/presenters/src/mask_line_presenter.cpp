/*
 * File:        mask_line_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Presenter for Mask Line analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/mask_line_presenter.h"
#include "../../core/include/project.h"
#include "../../core/include/dag_executor.h"
#include "../../core/include/logging.h"
#include "../../core/analysis/analysis_registry.h"
#include "../../core/analysis/analysis_progress.h"
#include "../../core/stages/stage.h"
#include <algorithm>

namespace orc::presenters {

MaskLinePresenter::MaskLinePresenter(void* project_handle)
    : AnalysisToolPresenter(project_handle) {
}

std::string MaskLinePresenter::toolId() const {
    return "mask_line_config";
}

std::string MaskLinePresenter::toolName() const {
    return "Configure Line Masking";
}

bool MaskLinePresenter::validateNode(NodeID node_id, std::string& error_message) {
    auto dag_void = getOrBuildDAG();
    if (!dag_void) {
        error_message = "Failed to build DAG from project";
        return false;
    }
    auto dag = std::static_pointer_cast<orc::DAG>(dag_void);
    
    const auto& dag_nodes = dag->nodes();
    auto node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
        [&node_id](const DAGNode& node) { return node.node_id == node_id; });
    
    if (node_it == dag_nodes.end()) {
        error_message = "Node not found in DAG";
        return false;
    }
    
    if (!node_it->stage || node_it->stage->get_node_type_info().stage_name != "mask_line") {
        error_message = "Line Masking Configuration only applies to mask_line stages";
        return false;
    }
    
    return true;
}

orc::public_api::AnalysisResult MaskLinePresenter::runAnalysis(
    NodeID node_id,
    const std::map<std::string, orc::ParameterValue>& parameters,
    std::function<void(int, const std::string&)> progress_callback) {
    
    orc::public_api::AnalysisResult result;
    result.status = orc::public_api::AnalysisResult::Status::Failed;
    
    // Report progress (instant tool, so go straight to 100%)
    if (progress_callback) {
        progress_callback(100, "Generating line masking configuration...");
    }
    
    // Get the analysis tool from registry
    auto tool = orc::AnalysisRegistry::instance().findById(toolId());
    if (!tool) {
        result.summary = "Line Masking Configuration tool not found in registry";
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }
    
    // Build DAG for validation only (tool doesn't need to execute anything)
    auto dag_void = getOrBuildDAG();
    if (!dag_void) {
        result.summary = "Failed to build DAG from project";
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }
    auto dag = std::static_pointer_cast<orc::DAG>(dag_void);
    
    // Validate node
    std::string error_message;
    if (!validateNode(node_id, error_message)) {
        result.summary = error_message;
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }
    
    // Build analysis context
    // Note: mask_line tool is a configuration tool, it doesn't need DAG execution
    // It just converts user checkbox selections to line specification strings
    orc::AnalysisContext ctx;
    ctx.source_type = orc::AnalysisSourceType::LaserDisc;
    ctx.node_id = node_id;
    ctx.parameters = parameters;
    
    // No DAG/project needed for this instant configuration tool
    ctx.dag = nullptr;
    ctx.project = nullptr;
    
    // Create progress adapter (though tool completes instantly)
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
            // Not used for this presenter
        }
        
    private:
        std::function<void(int, const std::string&)> callback_;
        std::atomic<bool> cancelled_;
        int current_percentage_;
        std::string current_status_;
    };
    
    ProgressAdapter progress_adapter(progress_callback);
    
    // Run the analysis tool
    auto core_result = tool->analyze(ctx, &progress_adapter);
    
    // Convert core result to public API result
    result.summary = core_result.summary;
    result.graphData = core_result.graphData;
    result.parameterChanges = core_result.parameterChanges;
    
    // Map status
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
    
    ORC_LOG_INFO("Mask line configuration completed: {}", 
                 result.status == orc::public_api::AnalysisResult::Status::Success ? "success" : "failed");
    
    return result;
}

} // namespace orc::presenters
