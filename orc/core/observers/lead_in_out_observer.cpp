/*
 * File:        lead_in_out_observer.cpp
 * Module:      orc-core
 * Purpose:     Lead-in/lead-out frame detection implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "lead_in_out_observer.h"
#include "observation_history.h"
#include "biphase_observer.h"
#include "logging.h"

namespace orc {

std::vector<std::shared_ptr<Observation>> LeadInOutObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<LeadInOutObservation>();
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
    
    // Get VBI observation
    auto biphase_obs_ptr = history.get_observation(field_id, "Biphase");
    if (!biphase_obs_ptr) {
        // Can't determine without VBI data
        observation->confidence = ConfidenceLevel::LOW;
        observation->is_lead_in_out = false;
        observations.push_back(observation);
        return observations;
    }
    
    auto biphase_obs = std::dynamic_pointer_cast<BiphaseObservation>(biphase_obs_ptr);
    if (!biphase_obs) {
        observation->confidence = ConfidenceLevel::LOW;
        observation->is_lead_in_out = false;
        observations.push_back(observation);
        return observations;
    }
    
    // Check for lead markers in VBI
    bool has_lead_marker = check_vbi_lead_markers(biphase_obs.get());
    
    // Check for illegal CAV frame number (frame 0)
    bool has_illegal_frame = false;
    if (biphase_obs->picture_number.has_value()) {
        has_illegal_frame = is_illegal_cav_frame_number(biphase_obs->picture_number.value());
    }
    
    // Determine if this is lead-in/out
    observation->is_lead_in_out = has_lead_marker || has_illegal_frame;
    
    // Try to distinguish lead-in vs lead-out
    // (Simple heuristic: early in capture = lead-in, late = lead-out)
    if (observation->is_lead_in_out) {
        auto field_range = representation.field_range();
        if (field_id.value() < field_range.start.value() + 100) {
            observation->is_lead_in = true;
        } else if (field_id.value() > field_range.end.value() - 100) {
            observation->is_lead_out = true;
        }
        observation->confidence = ConfidenceLevel::HIGH;
    } else {
        observation->confidence = ConfidenceLevel::HIGH;
    }
    
    if (observation->is_lead_in_out) {
        ORC_LOG_DEBUG("LeadInOutObserver: Field {} is lead-in/out (marker={} illegal_frame={})",
                      field_id.value(), has_lead_marker, has_illegal_frame);
    }
    
    observations.push_back(observation);
    return observations;
}

bool LeadInOutObserver::check_vbi_lead_markers(
    const BiphaseObservation* vbi_obs) const {
    
    if (!vbi_obs) {
        return false;
    }
    
    // Check for lead-in/out flags in VBI data
    // These are typically encoded in the VBI control bits
    // NOTE: BiphaseObservation currently doesn't have lead-in/out flags
    // TODO: Add these fields to BiphaseObservation structure
    // For now, return false (conservative)
    
    return false;
}

bool LeadInOutObserver::is_illegal_cav_frame_number(int32_t picture_number) const {
    // CAV discs should not have frame number 0 in the program area
    // Frame 0 typically indicates lead-in
    return picture_number == 0;
}

} // namespace orc
