/*
 * File:        dropout_analysis_observer.h
 * Module:      orc-core
 * Purpose:     Dropout analysis observer for aggregate dropout statistics
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "observer.h"
#include <cstdint>

namespace orc {

/**
 * @brief Analysis mode for dropout statistics
 */
enum class DropoutAnalysisMode {
    FULL_FIELD,      ///< Analyze all dropouts in the field
    VISIBLE_AREA     ///< Analyze only dropouts in the visible/active area
};

/**
 * @brief Observation for dropout analysis statistics
 * 
 * This observer tracks aggregate dropout statistics for each field,
 * useful for generating dropout analysis graphs showing dropout
 * density across the source.
 */
class DropoutAnalysisObservation : public Observation {
public:
    /// Total length of all dropouts in samples (full field or visible area depending on mode)
    double total_dropout_length = 0.0;
    
    /// Number of dropout regions detected
    size_t dropout_count = 0;
    
    /// Analysis mode used
    DropoutAnalysisMode mode = DropoutAnalysisMode::FULL_FIELD;
    
    /// Frame number (if available from VBI)
    std::optional<int32_t> frame_number;
    
    std::string observation_type() const override {
        return "DropoutAnalysis";
    }
};

/**
 * @brief Observer for dropout analysis statistics
 * 
 * Analyzes dropout hints from the source and calculates aggregate
 * statistics. Supports two modes:
 * - FULL_FIELD: Counts all dropouts in the field
 * - VISIBLE_AREA: Counts only dropouts in the active/visible area
 * 
 * The visible area is defined by the video parameters:
 * - Horizontal: activeVideoStart to activeVideoEnd
 * - Vertical: firstActiveFieldLine to lastActiveFieldLine
 * 
 * This is equivalent to ld-analyse's dropout analysis functionality.
 */
class DropoutAnalysisObserver : public Observer {
public:
    DropoutAnalysisObserver(DropoutAnalysisMode mode = DropoutAnalysisMode::FULL_FIELD)
        : mode_(mode) {}
    
    std::string observer_name() const override {
        return "DropoutAnalysisObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) override;
    
    /**
     * @brief Set the analysis mode
     */
    void set_mode(DropoutAnalysisMode mode) {
        mode_ = mode;
    }
    
    /**
     * @brief Get the current analysis mode
     */
    DropoutAnalysisMode get_mode() const {
        return mode_;
    }

private:
    DropoutAnalysisMode mode_;
    
    /**
     * @brief Calculate dropout length for full field
     */
    double calculate_full_field_dropout_length(
        const std::vector<DropoutRegion>& dropouts) const;
    
    /**
     * @brief Calculate dropout length for visible area only
     */
    double calculate_visible_area_dropout_length(
        const std::vector<DropoutRegion>& dropouts,
        const VideoFieldRepresentation& representation,
        FieldID field_id) const;
};

} // namespace orc
