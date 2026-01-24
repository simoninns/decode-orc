/*
 * File:        analysis_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Analysis presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "analysis_presenter.h"
#include "../core/include/project.h"
#include "../core/analysis/analysis_registry.h"
#include "../core/analysis/analysis_tool.h"
#include <stdexcept>
#include <algorithm>

namespace orc::presenters {

class AnalysisPresenter::Impl {
public:
    explicit Impl(orc::Project* project)
        : project_(project)
        , is_running_(false)
    {
        if (!project_) {
            throw std::invalid_argument("Project cannot be null");
        }
    }
    
    // Helper method to get tool by ID
    orc::AnalysisTool* getToolById(const std::string& tool_id) const {
        auto& registry = orc::AnalysisRegistry::instance();
        return registry.findById(tool_id);
    }
    
    orc::Project* project_;
    std::shared_ptr<orc::Project> project;  // Shared pointer for context
    std::shared_ptr<orc::DAG> dag;          // DAG for analysis context
    bool is_running_;
};

AnalysisPresenter::AnalysisPresenter(orc::Project* project)
    : impl_(std::make_unique<Impl>(project))
{
}

AnalysisPresenter::~AnalysisPresenter() = default;

AnalysisPresenter::AnalysisPresenter(AnalysisPresenter&&) noexcept = default;
AnalysisPresenter& AnalysisPresenter::operator=(AnalysisPresenter&&) noexcept = default;

bool AnalysisPresenter::runSNRAnalysis(orc::NodeID node_id, AnalysisProgressCallback progress_callback)
{
    return false;
}

bool AnalysisPresenter::runDropoutAnalysis(orc::NodeID node_id, AnalysisProgressCallback progress_callback)
{
    return false;
}

bool AnalysisPresenter::runBurstAnalysis(orc::NodeID node_id, AnalysisProgressCallback progress_callback)
{
    return false;
}

bool AnalysisPresenter::runQualityAnalysis(orc::NodeID node_id, AnalysisProgressCallback progress_callback)
{
    return false;
}

void AnalysisPresenter::cancelAnalysis()
{
    impl_->is_running_ = false;
}

bool AnalysisPresenter::isAnalysisRunning() const
{
    return impl_->is_running_;
}

SNRAnalysisData AnalysisPresenter::getSNRAnalysis(orc::NodeID node_id) const
{
    return SNRAnalysisData{};
}

DropoutAnalysisData AnalysisPresenter::getDropoutAnalysis(orc::NodeID node_id) const
{
    return DropoutAnalysisData{};
}

BurstAnalysisData AnalysisPresenter::getBurstAnalysis(orc::NodeID node_id) const
{
    return BurstAnalysisData{};
}

QualityAnalysisData AnalysisPresenter::getQualityAnalysis(orc::NodeID node_id) const
{
    return QualityAnalysisData{};
}

bool AnalysisPresenter::hasAnalysisData(orc::NodeID node_id, AnalysisType type) const
{
    return false;
}

void AnalysisPresenter::setAnalysisParameters(orc::NodeID node_id, AnalysisType type,
                                              const std::map<std::string, std::string>& parameters)
{
}

std::map<std::string, std::string> AnalysisPresenter::getAnalysisParameters(orc::NodeID node_id, AnalysisType type) const
{
    return {};
}

bool AnalysisPresenter::exportToCSV(orc::NodeID node_id, AnalysisType type, const std::string& output_path) const
{
    return false;
}

// === Analysis Tool Registry (Phase 2.4) ===

std::vector<orc::public_api::AnalysisToolInfo> AnalysisPresenter::getAvailableTools() const
{
    std::vector<orc::public_api::AnalysisToolInfo> result;
    
    auto& registry = orc::AnalysisRegistry::instance();
    auto all_tools = registry.tools();
    
    for (const auto* tool : all_tools) {
        if (!tool) continue;
        
        orc::public_api::AnalysisToolInfo info;
        info.id = tool->id();
        info.name = tool->name();
        info.description = tool->description();
        info.category = tool->category();
        info.priority = tool->priority();
        // Note: applicable_stages not directly available from AnalysisTool interface
        // Would need to enumerate all stage types and test isApplicableToStage()
        result.push_back(std::move(info));
    }
    
    return result;
}

std::vector<orc::public_api::AnalysisToolInfo> AnalysisPresenter::getToolsForStage(const std::string& stage_name) const
{
    std::vector<orc::public_api::AnalysisToolInfo> result;
    
    auto& registry = orc::AnalysisRegistry::instance();
    auto all_tools = registry.tools();
    
    // Filter tools applicable to this stage
    for (const auto* tool : all_tools) {
        if (!tool || !tool->isApplicableToStage(stage_name)) {
            continue;
        }
        
        orc::public_api::AnalysisToolInfo info;
        info.id = tool->id();
        info.name = tool->name();
        info.description = tool->description();
        info.category = tool->category();
        info.priority = tool->priority();
        info.applicable_stages.push_back(stage_name);
        
        result.push_back(std::move(info));
    }
    
    // Sort by priority (lower = first), then alphabetically
    std::sort(result.begin(), result.end(), 
        [](const orc::public_api::AnalysisToolInfo& a, const orc::public_api::AnalysisToolInfo& b) {
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            return a.name < b.name;
        });
    
    return result;
}

orc::public_api::AnalysisToolInfo AnalysisPresenter::getToolInfo(const std::string& tool_id) const
{
    auto& registry = orc::AnalysisRegistry::instance();
    auto* tool = registry.findById(tool_id);
    
    if (!tool) {
        return orc::public_api::AnalysisToolInfo{};  // Empty info
    }
    
    orc::public_api::AnalysisToolInfo info;
    info.id = tool->id();
    info.name = tool->name();
    info.description = tool->description();
    info.category = tool->category();
    info.priority = tool->priority();
    
    return info;
}

// === Generic Analysis Execution (Phase 2.8) ===

std::vector<orc::ParameterDescriptor> AnalysisPresenter::getToolParameters(
    const std::string& tool_id,
    orc::public_api::AnalysisSourceType source_type
) const {
    auto* tool = impl_->getToolById(tool_id);
    if (!tool) {
        return {};
    }
    
    // Create minimal context for parameter query
    orc::AnalysisContext context;
    context.source_type = static_cast<orc::AnalysisSourceType>(source_type);
    context.project = impl_->project;
    context.dag = impl_->dag;
    
    return tool->parametersForContext(context);
}

orc::public_api::AnalysisResult AnalysisPresenter::runGenericAnalysis(
    const std::string& tool_id,
    NodeID node_id,
    orc::public_api::AnalysisSourceType source_type,
    const std::map<std::string, orc::ParameterValue>& parameters,
    std::function<void(int current, int total, const std::string& status, const std::string& sub_status)> progress_callback
) const {
    auto* tool = impl_->getToolById(tool_id);
    if (!tool) {
        orc::public_api::AnalysisResult result;
        result.status = orc::public_api::AnalysisResult::Status::Failed;
        result.summary = "Analysis tool not found: " + tool_id;
        return result;
    }
    
    // Create analysis context
    orc::AnalysisContext context;
    context.source_type = static_cast<orc::AnalysisSourceType>(source_type);
    context.node_id = node_id;
    context.parameters = parameters;
    context.dag = impl_->dag;
    context.project = impl_->project;
    
    // Create progress wrapper
    class PresenterProgress : public orc::AnalysisProgress {
    public:
        PresenterProgress(std::function<void(int, int, const std::string&, const std::string&)> callback)
            : callback_(callback), current_(0), total_(100) {}
        
        void setProgress(int percentage) override {
            if (callback_) {
                callback_(percentage, 100, status_, sub_status_);
            }
        }
        
        void setStatus(const std::string& status) override {
            status_ = status;
            if (callback_) {
                callback_(current_, total_, status_, sub_status_);
            }
        }
        
        void setSubStatus(const std::string& sub_status) override {
            sub_status_ = sub_status;
            if (callback_) {
                callback_(current_, total_, status_, sub_status_);
            }
        }
        
        bool isCancelled() const override {
            return cancelled_;
        }
        
        void reportPartialResult(const AnalysisResult::ResultItem& item) override {
            // Optionally handle partial results in future
            (void)item;
        }
        
        void cancel() {
            cancelled_ = true;
        }
        
    private:
        std::function<void(int, int, const std::string&, const std::string&)> callback_;
        std::string status_;
        std::string sub_status_;
        int current_;
        int total_;
        bool cancelled_ = false;
    };
    
    auto progress = std::make_shared<PresenterProgress>(progress_callback);
    
    // Run analysis
    orc::AnalysisResult core_result = tool->analyze(context, progress.get());
    
    // Convert to public API result
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
    result.statistics = core_result.statistics;
    result.graphData = core_result.graphData;
    result.parameterChanges = core_result.parameterChanges;
    
    // Convert result items
    for (const auto& core_item : core_result.items) {
        orc::public_api::AnalysisResultItem item;
        item.type = core_item.type;
        item.message = core_item.message;
        item.startFrame = core_item.startFrame;
        item.endFrame = core_item.endFrame;
        item.metadata = core_item.metadata;
        result.items.push_back(item);
    }
    
    return result;
}

} // namespace orc::presenters
