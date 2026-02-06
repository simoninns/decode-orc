/*
 * File:        field_map_range_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Presenter for Field Map Range Finder analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/field_map_range_presenter.h"
#include "../../core/include/project.h"
#include "../../core/include/logging.h"
#include "../../core/include/dag_executor.h"
#include "../../core/analysis/analysis_registry.h"
#include "../../core/analysis/analysis_tool.h"
#include "../../core/analysis/analysis_progress.h"
#include "../../core/stages/stage.h"
#include <algorithm>
#include <memory>
#include <utility>

namespace orc::presenters {

FieldMapRangePresenter::FieldMapRangePresenter(void* project_handle)
    : AnalysisToolPresenter(project_handle) {
}

std::string FieldMapRangePresenter::toolId() const {
    return "field_map_range";
}

std::string FieldMapRangePresenter::toolName() const {
    return "Field Map Range Finder";
}

orc::AnalysisResult FieldMapRangePresenter::runAnalysis(
    NodeID node_id,
    const std::map<std::string, orc::ParameterValue>& parameters,
    std::function<void(int, const std::string&)> progress_callback) {

    orc::AnalysisResult result;
    result.status = orc::AnalysisResult::Status::Failed;

    class ProgressAdapter : public orc::AnalysisProgress {
    public:
        explicit ProgressAdapter(std::function<void(int, const std::string&)> cb)
            : callback_(std::move(cb)) {}

        void setProgress(int percentage) override {
            last_progress_ = percentage;
            if (callback_) {
                callback_(percentage, status_);
            }
        }

        void setStatus(const std::string& status) override {
            status_ = status;
            if (callback_) {
                callback_(last_progress_, status_);
            }
        }

        void setSubStatus(const std::string& sub_status) override {
            if (callback_) {
                std::string combined = status_;
                if (!combined.empty() && !sub_status.empty()) {
                    combined += " - ";
                }
                combined += sub_status;
                callback_(last_progress_, combined);
            }
        }

        bool isCancelled() const override { return false; }

        void reportPartialResult(const orc::AnalysisResult::ResultItem&) override {}

    private:
        std::function<void(int, const std::string&)> callback_;
        int last_progress_ = 0;
        std::string status_;
    } progress(progress_callback);

    if (progress_callback) {
        progress_callback(0, "Initializing field map range analysis...");
    }

    auto* tool = orc::AnalysisRegistry::instance().findById(toolId());
    if (!tool) {
        result.summary = "Field Map Range Finder tool not found in registry";
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }

    auto dag_void = getOrBuildDAG();
    if (!dag_void) {
        result.summary = "Failed to build DAG from project";
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }
    auto dag = std::static_pointer_cast<orc::DAG>(dag_void);

    const auto& nodes = dag->nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const DAGNode& n) { return n.node_id == node_id; });

    if (node_it == nodes.end()) {
        result.summary = "Node not found in DAG";
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }

    if (!node_it->stage || node_it->stage->get_node_type_info().stage_name != "field_map") {
        result.summary = "Field Map Range Finder only applies to field_map stages";
        ORC_LOG_ERROR("{}", result.summary);
        return result;
    }

    if (progress_callback) {
        progress_callback(10, "Preparing analysis context...");
    }

    orc::AnalysisContext ctx;
    ctx.source_type = orc::AnalysisSourceType::LaserDisc;
    ctx.node_id = node_id;
    ctx.parameters = parameters;
    ctx.dag = dag;
    ctx.project = std::make_shared<orc::Project>(*static_cast<orc::Project*>(getProjectPointer()));

    if (progress_callback) {
        progress_callback(20, "Running field map range analysis...");
    }

    orc::AnalysisResult core_result = tool->analyze(ctx, &progress);

    if (progress_callback) {
        if (core_result.status == orc::AnalysisResult::Success) {
            progress_callback(100, "Analysis complete");
        } else {
            progress_callback(0, "Analysis failed");
        }
    }

    switch (core_result.status) {
        case orc::AnalysisResult::Success:
            result.status = orc::AnalysisResult::Status::Success;
            break;
        case orc::AnalysisResult::Failed:
            result.status = orc::AnalysisResult::Status::Failed;
            break;
        case orc::AnalysisResult::Cancelled:
            result.status = orc::AnalysisResult::Status::Cancelled;
            break;
    }

    result.summary = core_result.summary;
    result.statistics = core_result.statistics;
    result.graphData = core_result.graphData;
    result.parameterChanges = core_result.parameterChanges;

    for (const auto& item : core_result.items) {
        orc::AnalysisResultItem api_item;
        api_item.type = item.type;
        api_item.message = item.message;
        api_item.startFrame = item.startFrame;
        api_item.endFrame = item.endFrame;
        api_item.metadata = item.metadata;
        result.items.push_back(std::move(api_item));
    }

    return result;
}

} // namespace orc::presenters
