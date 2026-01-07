/*
 * File:        lead_in_out_observer.h
 * Module:      orc-core
 * Purpose:     Lead-in/lead-out frame detection observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "observer.h"
#include <cstdint>

namespace orc {

/**
 * @brief Observation for lead-in/lead-out frame detection
 * 
 * LaserDisc lead-in and lead-out areas contain special codes and
 * should typically be excluded from processing.
 */
class LeadInOutObservation : public Observation {
public:
    bool is_lead_in_out = false;
    bool is_lead_in = false;   // True if specifically lead-in
    bool is_lead_out = false;  // True if specifically lead-out
    
    std::string observation_type() const override {
        return "LeadInOut";
    }
};

/**
 * @brief Observer for lead-in/lead-out frame detection
 * 
 * Detects lead-in and lead-out frames using VBI codes:
 * - CAV: Frame number 0 or special lead codes
 * - CLV: Time code 00:00:00.00 or lead markers
 * - Special VBI flags indicating lead areas
 */
class LeadInOutObserver : public Observer {
public:
    LeadInOutObserver() = default;
    
    std::string observer_name() const override {
        return "LeadInOutObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) override;

private:
    /**
     * @brief Check VBI data for lead-in/out indicators
     */
    bool check_vbi_lead_markers(
        const class BiphaseObservation* vbi_obs) const;
    
    /**
     * @brief Check for illegal CAV frame numbers
     * 
     * CAV frame 0 is typically illegal and indicates lead-in
     */
    bool is_illegal_cav_frame_number(int32_t picture_number) const;
};

} // namespace orc
