/*
 * File:        observation_history.cpp
 * Module:      orc-core
 * Purpose:     Observation history implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "observation_history.h"

namespace orc {

void ObservationHistory::add_observations(
    FieldID field_id,
    const std::vector<std::shared_ptr<Observation>>& observations) {
    
    if (!field_id.is_valid()) {
        return;
    }
    
    history_[field_id] = observations;
}

std::vector<std::shared_ptr<Observation>> ObservationHistory::get_observations(
    FieldID field_id) const {
    
    auto it = history_.find(field_id);
    if (it != history_.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::shared_ptr<Observation>> ObservationHistory::get_observations(
    FieldID field_id,
    const std::string& observation_type) const {
    
    std::vector<std::shared_ptr<Observation>> result;
    
    auto all_obs = get_observations(field_id);
    for (const auto& obs : all_obs) {
        if (obs && obs->observation_type() == observation_type) {
            result.push_back(obs);
        }
    }
    
    return result;
}

std::shared_ptr<Observation> ObservationHistory::get_observation(
    FieldID field_id,
    const std::string& observation_type) const {
    
    auto observations = get_observations(field_id, observation_type);
    if (!observations.empty()) {
        return observations[0];
    }
    return nullptr;
}

bool ObservationHistory::has_field(FieldID field_id) const {
    return history_.find(field_id) != history_.end();
}

FieldID ObservationHistory::get_latest_field() const {
    if (history_.empty()) {
        return FieldID();  // Invalid
    }
    
    // Map is ordered by FieldID, so last element is latest
    return history_.rbegin()->first;
}

void ObservationHistory::clear() {
    history_.clear();
}

} // namespace orc
