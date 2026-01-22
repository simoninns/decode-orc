/*
 * File:        hints_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Hints presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "hints_presenter.h"
#include "../core/include/project.h"
#include <stdexcept>

namespace orc::presenters {

class HintsPresenter::Impl {
public:
    explicit Impl(orc::Project* project)
        : project_(project)
        , next_hint_id_(1)
    {
        if (!project_) {
            throw std::invalid_argument("Project cannot be null");
        }
    }
    
    orc::Project* project_;
    int next_hint_id_;
};

HintsPresenter::HintsPresenter(orc::Project* project)
    : impl_(std::make_unique<Impl>(project))
{
}

HintsPresenter::~HintsPresenter() = default;

HintsPresenter::HintsPresenter(HintsPresenter&&) noexcept = default;
HintsPresenter& HintsPresenter::operator=(HintsPresenter&&) noexcept = default;

int HintsPresenter::addActiveLineHint(NodeID node_id, const ActiveLineHint& hint)
{
    return impl_->next_hint_id_++;
}

int HintsPresenter::addVBIHint(NodeID node_id, const VBIHint& hint)
{
    return impl_->next_hint_id_++;
}

int HintsPresenter::addDropoutHint(NodeID node_id, const DropoutHint& hint)
{
    return impl_->next_hint_id_++;
}

int HintsPresenter::addBurstHint(NodeID node_id, const BurstHint& hint)
{
    return impl_->next_hint_id_++;
}

bool HintsPresenter::updateHint(int hint_id, const Hint& hint)
{
    return false;
}

bool HintsPresenter::removeHint(int hint_id)
{
    return false;
}

void HintsPresenter::setHintEnabled(int hint_id, bool enabled)
{
}

std::vector<Hint> HintsPresenter::getHints(NodeID node_id) const
{
    return {};
}

std::vector<Hint> HintsPresenter::getHintsByType(NodeID node_id, HintType type) const
{
    return {};
}

Hint HintsPresenter::getHint(int hint_id) const
{
    Hint h;
    h.id = hint_id;
    h.type = HintType::ActiveLine;
    return h;
}

bool HintsPresenter::hasHints(NodeID node_id) const
{
    return false;
}

std::vector<DropoutHint> HintsPresenter::getDropoutHintsForField(NodeID node_id, FieldID field_id) const
{
    return {};
}

bool HintsPresenter::validateHint(const Hint& hint, std::string* error_message) const
{
    return true;
}

void HintsPresenter::clearHints(NodeID node_id)
{
}

bool HintsPresenter::importHints(NodeID node_id, const std::string& file_path)
{
    return false;
}

bool HintsPresenter::exportHints(NodeID node_id, const std::string& file_path) const
{
    return false;
}

} // namespace orc::presenters
