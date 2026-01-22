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
#include <stdexcept>

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

} // namespace orc::presenters
