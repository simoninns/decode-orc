/*
 * File:        burst_level_observer.h
 * Module:      orc-core
 * Purpose:     Color burst median IRE level observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "observer.h"
#include <vector>

namespace orc {

/**
 * @brief Observer for color burst IRE level analysis
 * 
 * Analyzes the color burst signal amplitude and reports it in IRE units.
 * The burst level is useful for quality assessment and can indicate
 * signal degradation or processing artifacts.
 * 
 * Stores observations in the "burst_level" namespace:
 * - "median_burst_ire" (double): Median burst amplitude in IRE units
 */
class BurstLevelObserver : public Observer {
public:
    BurstLevelObserver() = default;
    ~BurstLevelObserver() override = default;
    
    std::string observer_name() const override {
        return "BurstLevelObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    void process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        ObservationContext& context) override;
    
    std::vector<ObservationKey> get_provided_observations() const override {
        return {
            {"burst_level", "median_burst_ire", ObservationType::DOUBLE, "Median color burst amplitude in IRE"},
        };
    }
    
private:
    // Calculate median of samples
    double calculate_median(std::vector<double> values) const;
};

} // namespace orc
