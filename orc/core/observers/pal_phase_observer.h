/*
 * File:        pal_phase_observer.h
 * Module:      orc-core
 * Purpose:     PAL field phase ID observer
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

// PAL phase observation
class PALPhaseObservation : public Observation {
public:
    int32_t field_phase_id;    // PAL phase (1-8), or -1 if unable to determine
    
    std::string observation_type() const override {
        return "PALPhase";
    }
};

// PAL field phase ID observer (determines position in 8-field PAL sequence)
class PALPhaseObserver : public Observer {
public:
    PALPhaseObserver() = default;
    
    std::string observer_name() const override {
        return "PALPhaseObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) override;
    
private:
    // Get burst level for a specific line
    std::optional<double> get_burst_level(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        size_t line,
        const VideoParameters& video_params) const;
    
    // Measure burst phase on a specific line (in degrees)
    std::optional<double> measure_burst_phase(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        size_t line,
        const VideoParameters& video_params) const;
    
    // Calculate RMS of a signal
    double calculate_rms(const std::vector<double>& data) const;
};

} // namespace orc
