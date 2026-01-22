/*
 * File:        dropout_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Dropout presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "dropout_presenter.h"
#include "../core/include/project.h"
#include <stdexcept>

namespace orc::presenters {

class DropoutPresenter::Impl {
public:
    explicit Impl(orc::Project* project)
        : project_(project)
    {
        if (!project_) {
            throw std::invalid_argument("Project cannot be null");
        }
    }
    
    orc::Project* project_;
};

DropoutPresenter::DropoutPresenter(orc::Project* project)
    : impl_(std::make_unique<Impl>(project))
{
}

DropoutPresenter::~DropoutPresenter() = default;

DropoutPresenter::DropoutPresenter(DropoutPresenter&&) noexcept = default;
DropoutPresenter& DropoutPresenter::operator=(DropoutPresenter&&) noexcept = default;

std::vector<DetectedDropout> DropoutPresenter::detectDropouts(NodeID node_id, FieldID field_id)
{
    return {};
}

std::vector<DetectedDropout> DropoutPresenter::getDetectedDropouts(NodeID node_id, FieldID field_id) const
{
    return {};
}

void DropoutPresenter::clearDetections(NodeID node_id, FieldID field_id)
{
}

void DropoutPresenter::updateDropoutDecision(NodeID node_id, FieldID field_id, 
                                              int line, int pixel_start,
                                              DropoutDecision decision,
                                              const std::string& correction_method)
{
}

std::vector<DropoutCorrection> DropoutPresenter::getCorrections(NodeID node_id, FieldID field_id) const
{
    return {};
}

void DropoutPresenter::removeCorrection(NodeID node_id, FieldID field_id, int line, int pixel_start)
{
}

void DropoutPresenter::clearCorrections(NodeID node_id, FieldID field_id)
{
}

FieldDropoutStats DropoutPresenter::getFieldStats(NodeID node_id, FieldID field_id) const
{
    return FieldDropoutStats{};
}

std::map<FieldID, FieldDropoutStats> DropoutPresenter::getAllStats(NodeID node_id) const
{
    return {};
}

int DropoutPresenter::applyDecisionToSimilar(NodeID node_id, FieldID field_id,
                                              const DetectedDropout& reference_dropout,
                                              DropoutDecision decision)
{
    return 0;
}

int DropoutPresenter::autoDecideDropouts(NodeID node_id, FieldID field_id, double severity_threshold)
{
    return 0;
}

bool DropoutPresenter::exportCorrections(NodeID node_id, const std::string& file_path) const
{
    return false;
}

bool DropoutPresenter::importCorrections(NodeID node_id, const std::string& file_path)
{
    return false;
}

} // namespace orc::presenters
