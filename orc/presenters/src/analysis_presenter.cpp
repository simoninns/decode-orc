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
    
    orc::Project* project_;
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

orc::AnalysisTool* AnalysisPresenter::getToolById(const std::string& tool_id) const
{
    auto& registry = orc::AnalysisRegistry::instance();
    return registry.findById(tool_id);
}

} // namespace orc::presenters
