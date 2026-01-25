/*
 * File:        ffmpeg_preset_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Presenter for FFmpeg Preset analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/ffmpeg_preset_presenter.h"
#include "../../core/include/project.h"
#include "../../core/include/dag_executor.h"
#include "../../core/include/logging.h"
#include "../../core/analysis/analysis_registry.h"
#include "../../core/analysis/analysis_progress.h"
#include "../../core/stages/stage.h"
#include <algorithm>

namespace orc::presenters {

FFmpegPresetPresenter::FFmpegPresetPresenter(void* project_handle)
    : AnalysisToolPresenter(project_handle) {
}

std::string FFmpegPresetPresenter::toolId() const {
    return "ffmpeg_preset_config";
}

std::string FFmpegPresetPresenter::toolName() const {
    return "Configure Export Preset";
}

bool FFmpegPresetPresenter::validateNode(NodeID node_id, std::string& error_message) {
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
    
    if (!node_it->stage || node_it->stage->get_node_type_info().stage_name != "ffmpeg_video_sink") {
        error_message = "FFmpeg Preset Configuration only applies to ffmpeg_video_sink stages";
        return false;
    }
    
    return true;
}

orc::public_api::AnalysisResult FFmpegPresetPresenter::runAnalysis(
    NodeID node_id,
    const std::map<std::string, orc::ParameterValue>& parameters,
    std::function<void(int, const std::string&)> progress_callback) {
    
    orc::public_api::AnalysisResult result;
    result.status = orc::public_api::AnalysisResult::Status::Failed;
    
    // Report progress (instant tool, so go straight to 100%)
    if (progress_callback) {
        progress_callback(100, "Generating FFmpeg export preset...");
    }
    
    // Get the analysis tool from registry
    auto tool = orc::AnalysisRegistry::instance().findById(toolId());
    if (!tool) {
        result.summary = "FFmpeg Preset Configuration tool not found in registry";
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
    // Note: FFmpeg preset tool is a configuration tool, it doesn't need DAG execution
    // The actual configuration is handled by FFmpegPresetDialog in the GUI
    orc::AnalysisContext ctx;
    ctx.source_type = orc::AnalysisSourceType::LaserDisc;
    ctx.node_id = node_id;
    ctx.parameters = parameters;
    // ctx.project not set - tool doesn't need it
    // ctx.dag not set - tool doesn't execute DAG
    
    // Create progress adapter
    class ProgressAdapter : public orc::AnalysisProgress {
    public:
        explicit ProgressAdapter(std::function<void(int, const std::string&)> callback)
            : callback_(callback), current_percentage_(0) {}
        
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
            return false;  // No cancellation support for instant tools
        }
        
        void reportPartialResult(const orc::AnalysisResult::ResultItem&) override {
            // Not used for instant configuration tools
        }
        
    private:
        std::function<void(int, const std::string&)> callback_;
        int current_percentage_;
        std::string current_status_;
    };
    
    ProgressAdapter progress_adapter(progress_callback);
    
    // Run the analysis
    ORC_LOG_INFO("Running FFmpeg preset configuration for node {}", node_id);
    auto core_result = tool->analyze(ctx, &progress_adapter);
    
    // Convert core result to public API result
    result.summary = core_result.summary;
    result.graphData = core_result.graphData;
    result.parameterChanges = core_result.parameterChanges;
    
    // Convert status
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
    
    // Note: graphData not used by this tool - configuration is applied via FFmpegPresetDialog
    
    if (result.status == orc::public_api::AnalysisResult::Status::Success) {
        ORC_LOG_INFO("FFmpeg preset configuration completed successfully");
    } else {
        ORC_LOG_ERROR("FFmpeg preset configuration failed: {}", result.summary);
    }
    
    return result;
}

} // namespace orc::presenters
