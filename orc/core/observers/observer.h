/*
 * File:        observer.h
 * Module:      orc-core
 * Purpose:     Observer base class
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include "field_id.h"
#include "video_field_representation.h"
#include <string>
#include <memory>
#include <optional>
#include <map>
#include <vector>

namespace orc {

// Forward declaration
class ObservationHistory;

// Detection basis for observations
enum class DetectionBasis {
    SAMPLE_DERIVED,      // Derived purely from sample analysis
    HINT_DERIVED,        // Derived from external hints
    CORROBORATED         // Sample evidence corroborates hints
};

// Confidence level for observations
enum class ConfidenceLevel {
    NONE,                // No valid observation
    LOW,                 // Low confidence
    MEDIUM,              // Medium confidence
    HIGH                 // High confidence
};

// Base class for all observations
class Observation {
public:
    virtual ~Observation() = default;
    
    FieldID field_id;
    DetectionBasis detection_basis;
    ConfidenceLevel confidence;
    std::string observer_version;
    std::map<std::string, std::string> observer_parameters;
    
    virtual std::string observation_type() const = 0;
};

// Base class for observers
class Observer {
public:
    virtual ~Observer() = default;
    
    // Observer metadata
    virtual std::string observer_name() const = 0;
    virtual std::string observer_version() const = 0;
    
    // Process a single field and return observations
    // The history parameter provides access to observations from previous fields
    virtual std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) = 0;
    
    // Optional: Set parameters for the observer
    virtual void set_parameters(const std::map<std::string, std::string>& params) {
        parameters_ = params;
    }
    
    // Optional: Provide hints for the observer
    virtual void set_hints(const std::map<FieldID, std::vector<std::string>>& hints) {
        hints_ = hints;
    }
    
protected:
    std::map<std::string, std::string> parameters_;
    std::map<FieldID, std::vector<std::string>> hints_;
};

} // namespace orc
