/*
 * File:        burst_level_observer.h
 * Module:      orc-core
 * Purpose:     Color burst median IRE level observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "observer.h"
#include "tbc_metadata.h"
#include <optional>
#include <vector>

namespace orc {

// Burst level observation
class BurstLevelObservation : public Observation {
public:
    double median_burst_ire;    // Median IRE level of color burst
    
    std::string observation_type() const override {
        return "BurstLevel";
    }
};

// Color burst median IRE level observer
class BurstLevelObserver : public Observer {
public:
    BurstLevelObserver() = default;
    
    std::string observer_name() const override {
        return "BurstLevelObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) override;
    
private:
    // Calculate median of samples
    double calculate_median(std::vector<double> values) const;
    
    // Convert 16-bit sample to IRE
    double sample_to_ire(uint16_t sample, uint16_t black_level, uint16_t white_level) const;
};

} // namespace orc
