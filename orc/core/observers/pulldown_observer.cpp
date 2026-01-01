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
    bool phase_suggests_pulldown = analyze_phase_pattern(representation, field_id);
    bool vbi_suggests_pulldown = check_vbi_pattern(field_id, history);
    
    // Determine pattern position (0-4 in the 5-frame cycle)
    // Standard 3:2 pulldown: frames 1 and 3 in the 5-frame pattern have repeated fields
    if (biphase_obs->picture_number.has_value()) {
        uint32_t pic_num = biphase_obs->picture_number.value();
        observation->pattern_position = pic_num % 5;
    }
    
    // Check for pattern breaks by looking at consistency
    // If phase analysis and VBI disagree strongly, mark as pattern break
    if ((phase_suggests_pulldown && !vbi_suggests_pulldown) ||
        (!phase_suggests_pulldown && vbi_suggests_pulldown)) {
        // Evidence is contradictory - possible pattern break
        observation->pattern_break = true;
    }
    
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
    
    ORC_LOG_DEBUG("PulldownObserver: Field {} is_pulldown={} (phase={} vbi={}) pattern_pos={} break={}",
                  field_id.value(), observation->is_pulldown,
                  phase_suggests_pulldown, vbi_suggests_pulldown,
                  observation->pattern_position, observation->pattern_break);
    
    observations.push_back(observation);
    return observations;
}

bool PulldownObserver::analyze_phase_pattern(
    const VideoFieldRepresentation& representation,
    FieldID field_id) const {
    
    // NTSC 3:2 pulldown pattern:
    // - Film runs at 24fps, video at 29.97fps
    // - Each film frame becomes either 3 or 2 video fields
    // - Standard pattern: 3-2-3-2-3 (10 fields from 4 film frames)
    // - This creates phase repetitions detectable in the 4-field NTSC sequence
    //
    // Normal NTSC: 1-2-3-4-1-2-3-4-1-2-3-4...
    // With pulldown: 1-2-2-3-4-4-1-2-2-3-4-4... (phases repeat)
    //
    // We look for repeated phase IDs, which indicate pulldown fields
    
    // Get current field's phase
    auto current_phase_hint = representation.get_field_phase_hint(field_id);
    if (!current_phase_hint.has_value() || current_phase_hint->field_phase_id < 0) {
        // No phase information available
        return false;
    }
    
    int current_phase = current_phase_hint->field_phase_id;
    
    // Check previous field's phase
    if (field_id.value() == 0) {
        // No previous field to compare
        return false;
    }
    
    FieldID prev_id(field_id.value() - 1);
    auto prev_phase_hint = representation.get_field_phase_hint(prev_id);
    
    if (!prev_phase_hint.has_value() || prev_phase_hint->field_phase_id < 0) {
        // No previous phase information
        return false;
    }
    
    int prev_phase = prev_phase_hint->field_phase_id;
    
    // In normal NTSC, phase increments: 1->2, 2->3, 3->4, 4->1
    // In pulldown, phase may repeat: 2->2 or 4->4
    if (current_phase == prev_phase) {
        // Same phase as previous field - strong indicator of pulldown
        ORC_LOG_DEBUG("Phase repetition detected: field {} and {} both phase {}",
                      prev_id.value(), field_id.value(), current_phase);
        return true;
    }
    
    // Also check for the broader 5-frame pattern if we have enough history
    // Standard 3:2 pulldown creates a repeating 10-field pattern
    // We can look back 5 fields to see if there's a pattern match
    if (field_id.value() >= 10) {
        // Check if current phase matches phase from 10 fields ago
        // This would confirm we're in the repeating pulldown cycle
        FieldID pattern_id(field_id.value() - 10);
        auto pattern_phase_hint = representation.get_field_phase_hint(pattern_id);
        
        if (pattern_phase_hint.has_value() && pattern_phase_hint->field_phase_id >= 0) {
            int pattern_phase = pattern_phase_hint->field_phase_id;
            
            // In a pure pulldown pattern, phases repeat every 10 fields
            // But we need to account for the normal 4-field cycle too
            // Expected: (current - pattern) % 4 should be 2 (10 % 4 = 2)
            int phase_diff = (current_phase - pattern_phase + 4) % 4;
            
            // Due to pulldown, we might see irregular patterns
            // Look for phase repetition within tolerance
            if (phase_diff == 2 || phase_diff == 0) {
                // Check if we also see the characteristic phase repetition
                // in the surrounding fields
                int repetition_count = 0;
                for (int offset = 1; offset <= 5 && field_id.value() >= static_cast<uint64_t>(offset); offset++) {
                    FieldID check_id(field_id.value() - offset);
                    if (check_id.value() == 0) break;  // Can't get previous field
                    
                    FieldID check_prev_id(check_id.value() - 1);
                    
                    auto check_hint = representation.get_field_phase_hint(check_id);
                    auto check_prev_hint = representation.get_field_phase_hint(check_prev_id);
                    
                    if (check_hint.has_value() && check_prev_hint.has_value() &&
                        check_hint->field_phase_id >= 0 && check_prev_hint->field_phase_id >= 0) {
                        if (check_hint->field_phase_id == check_prev_hint->field_phase_id) {
                            repetition_count++;
                        }
                    }
                }
                
                // If we see 2 or more phase repetitions in the last 5 fields,
                // we're likely in a pulldown pattern (3:2 creates 2 repetitions per 5 frames)
                if (repetition_count >= 2) {
                    ORC_LOG_DEBUG("Pulldown pattern detected: {} phase repetitions in last 5 fields",
                                  repetition_count);
                    return true;
                }
            }
        }
    }
    
    return false;
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
