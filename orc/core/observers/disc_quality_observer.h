/*
 * File:        disc_quality_observer.h
 * Module:      orc-core
 * Purpose:     Disc quality observer for field quality metrics
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "observer.h"
#include <array>
#include <cstdint>

namespace orc {

/**
 * @brief Observation for disc quality metrics
 */
class DiscQualityObservation : public Observation {
public:
    double quality_score = 0.0;  // 0.0 (worst) to 1.0 (best)
    
    // Contributing factors (for diagnostics)
    size_t dropout_count = 0;
    double snr_estimate = 0.0;
    bool has_valid_phase = true;
    
    std::string observation_type() const override {
        return "DiscQuality";
    }
};

/**
 * @brief Observer for field quality analysis
 * 
 * Calculates a quality score for each field based on:
 * - Dropout count/density
 * - Phase correctness
 * - Signal-to-noise estimates (if available)
 * 
 * Used by disc mapping policy to choose best duplicate when multiple
 * fields have the same VBI frame number.
 */
class DiscQualityObserver : public Observer {
public:
    DiscQualityObserver() = default;
    
    std::string observer_name() const override {
        return "DiscQualityObserver";
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
     * @brief Calculate quality score from observations and hints
     * 
     * Combines multiple quality indicators:
     * - Dropout density (from hints)
     * - Phase correctness (from observations)
     * - SNR metrics (from VITS if available)
     * 
     * @return Quality score 0.0-1.0
     */
    double calculate_quality_score(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) const;
};

} // namespace orc
