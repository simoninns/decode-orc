/*
 * File:        pulldown_observer.h
 * Module:      orc-core
 * Purpose:     NTSC pulldown frame detection observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "observer.h"
#include <cstdint>

namespace orc {

/**
 * @brief Observation for pulldown frame detection
 * 
 * NTSC CAV discs use 3:2 pulldown resulting in repeated fields.
 * Standard pattern is 1-in-5 frames is a pulldown.
 */
class PulldownObservation : public Observation {
public:
    bool is_pulldown = false;
    
    // Diagnostic info
    int pattern_position = -1;  // Position in 5-frame pattern (0-4), -1 if unknown
    bool pattern_break = false;  // True if pattern is inconsistent
    
    std::string observation_type() const override {
        return "Pulldown";
    }
};

/**
 * @brief Observer for NTSC pulldown frame detection
 * 
 * Detects pulldown frames in NTSC CAV recordings using phase pattern analysis.
 * Standard 3:2 pulldown creates a 1-in-5 pattern of repeated fields.
 * 
 * Uses observation history to track phase sequences and detect patterns.
 * Only operates on NTSC format sources.
 */
class PulldownObserver : public Observer {
public:
    PulldownObserver() = default;
    
    std::string observer_name() const override {
        return "PulldownObserver";
    }
    
    std::string observer_version() const override {
        return "1.1.0";
    }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) override;

private:
    /**
     * @brief Analyze phase sequence to detect pulldown
     * 
     * Standard NTSC has 4-field phase sequence (1,2,3,4).
     * Pulldown introduces repeated fields, detectable in phase patterns.
     * 
     * @param representation Video field representation to access phase hints
     * @param field_id Current field
     * @return True if field appears to be pulldown
     */
    bool analyze_phase_pattern(
        const VideoFieldRepresentation& representation,
        FieldID field_id) const;
    
    /**
     * @brief Check if VBI frame number indicates pulldown
     * 
     * In some cases, pulldown frames may have same VBI number as previous frame
     * or special markers.
     * 
     * @param field_id Current field
     * @param history Observation history
     * @return True if VBI suggests pulldown
     */
    bool check_vbi_pattern(
        FieldID field_id,
        const ObservationHistory& history) const;
};

} // namespace orc
