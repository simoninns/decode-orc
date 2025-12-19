/*
 * File:        white_flag_observer.cpp
 * Module:      orc-core
 * Purpose:     White flag observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "white_flag_observer.h"
#include "tbc_video_field_representation.h"

namespace orc {

std::vector<std::shared_ptr<Observation>> WhiteFlagObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id) {
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<WhiteFlagObservation>();
    observation->field_id = field_id;
    observation->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    observation->observer_version = observer_version();
    observation->confidence = ConfidenceLevel::HIGH;  // Always detectable
    
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Only for NTSC
    if (descriptor->format != VideoFormat::NTSC) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Line 11 (0-based: 10)
    size_t line_num = 10;
    if (line_num >= descriptor->height) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    const uint16_t* line_data = representation.get_line(field_id, line_num);
    if (line_data == nullptr) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    uint16_t white_ire = 50000;
    uint16_t black_ire = 15000;
    uint16_t zero_crossing = (white_ire + black_ire) / 2;
    
    // Count samples above zero-crossing in active video region
    size_t active_start = descriptor->width / 8;
    size_t active_end = descriptor->width * 7 / 8;
    
    size_t white_count = 0;
    size_t total_count = active_end - active_start;
    
    for (size_t i = active_start; i < active_end; ++i) {
        if (line_data[i] > zero_crossing) {
            white_count++;
        }
    }
    
    // White flag if >50% above zero-crossing
    observation->white_flag_present = (white_count > total_count / 2);
    
    observations.push_back(observation);
    return observations;
}

} // namespace orc
