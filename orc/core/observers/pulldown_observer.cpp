/*
 * File:        pulldown_observer.cpp
 * Module:      orc-core
 * Purpose:     NTSC pulldown frame detection implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "pulldown_observer.h"
#include "observation_history.h"
#include "biphase_observer.h"
#include "logging.h"

namespace orc {

std::vector<std::shared_ptr<Observation>> PulldownObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<PulldownObservation>();
    observation->field_id = field_id;
    observation->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    observation->observer_version = observer_version();
    
    // Get field descriptor
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Only for NTSC
    if (descriptor->format != VideoFormat::NTSC) {
        observation->confidence = ConfidenceLevel::NONE;
        observation->is_pulldown = false;
        observations.push_back(observation);
        return observations;
    }
    
    // Need VBI observation to determine disc type (CAV only has pulldown)
    auto biphase_obs_ptr = history.get_observation(field_id, "Biphase");
    if (!biphase_obs_ptr) {
        // Can't determine without VBI data
        observation->confidence = ConfidenceLevel::LOW;
        observation->is_pulldown = false;
        observations.push_back(observation);
        return observations;
    }
    
    // Cast to BiphaseObservation to access specific members
    auto biphase_obs = std::dynamic_pointer_cast<BiphaseObservation>(biphase_obs_ptr);
    if (!biphase_obs) {
        observation->confidence = ConfidenceLevel::LOW;
        observation->is_pulldown = false;
        observations.push_back(observation);
        return observations;
    }
    
    // Check if this is a CAV disc (CLV doesn't have pulldown in same way)
    // CAV discs have picture numbers, CLV discs have timecodes
    bool is_cav = biphase_obs->picture_number.has_value();
    if (!is_cav) {
        observation->confidence = ConfidenceLevel::HIGH;
        observation->is_pulldown = false;
        observations.push_back(observation);
        return observations;
    }
    
    // Analyze phase and VBI patterns
    bool phase_suggests_pulldown = analyze_phase_pattern(field_id, history);
    bool vbi_suggests_pulldown = check_vbi_pattern(field_id, history);
    
    // Combine evidence
    if (phase_suggests_pulldown && vbi_suggests_pulldown) {
        observation->is_pulldown = true;
        observation->confidence = ConfidenceLevel::HIGH;
    } else if (phase_suggests_pulldown || vbi_suggests_pulldown) {
        observation->is_pulldown = true;
        observation->confidence = ConfidenceLevel::MEDIUM;
    } else {
        observation->is_pulldown = false;
        observation->confidence = ConfidenceLevel::HIGH;
    }
    
    ORC_LOG_DEBUG("PulldownObserver: Field {} is_pulldown={} (phase={} vbi={})",
                  field_id.value(), observation->is_pulldown,
                  phase_suggests_pulldown, vbi_suggests_pulldown);
    
    observations.push_back(observation);
    return observations;
}

bool PulldownObserver::analyze_phase_pattern(
    FieldID field_id,
    const ObservationHistory& history) const {
    
    // NTSC has 4-field phase sequence
    // Pulldown frames break this sequence by repeating phases
    // This is a simplified heuristic - full implementation would
    // track 5-frame patterns
    
    // For now, just check if we can find enough history to make a determination
    // Real implementation would look at phase sequence across last 5-10 fields
    
    // TODO: Implement full phase pattern analysis
    // For initial version, rely more heavily on VBI pattern
    
    (void)field_id;
    (void)history;
    
    return false;  // Conservative: don't claim pulldown from phase alone yet
}

bool PulldownObserver::check_vbi_pattern(
    FieldID field_id,
    const ObservationHistory& history) const {
    
    // Get current and previous VBI observations
    auto current_vbi_ptr = history.get_observation(field_id, "Biphase");
    if (!current_vbi_ptr) {
        return false;
    }
    
    auto current_vbi = std::dynamic_pointer_cast<BiphaseObservation>(current_vbi_ptr);
    if (!current_vbi) {
        return false;
    }
    
    // Check if current field has invalid/missing VBI frame number
    // Pulldown frames often don't have their own VBI number
    if (!current_vbi->picture_number.has_value() && current_vbi->confidence != ConfidenceLevel::NONE) {
        // Has VBI data but no picture number - possible pulldown
        return true;
    }
    
    // Check previous field
    if (field_id.value() > 0) {
        FieldID prev_id(field_id.value() - 1);
        auto prev_vbi_ptr = history.get_observation(prev_id, "Biphase");
        
        if (prev_vbi_ptr) {
            auto prev_vbi = std::dynamic_pointer_cast<BiphaseObservation>(prev_vbi_ptr);
            if (prev_vbi && current_vbi->picture_number.has_value() && 
                prev_vbi->picture_number.has_value() &&
                current_vbi->picture_number == prev_vbi->picture_number) {
                // Same picture number as previous field - likely pulldown
                return true;
            }
        }
    }
    
    return false;
}

} // namespace orc
