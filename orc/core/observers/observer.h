/*
 * File:        observer.h
 * Module:      orc-core
 * Purpose:     Observer base class
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#pragma once

#include <field_id.h>
#include "video_field_representation.h"
#include "observation_context.h"
#include "observation_schema.h"
#include "stage_parameter.h"
#include <string>
#include <memory>
#include <optional>
#include <map>
#include <vector>

namespace orc {

// Confidence level for observations
enum class ConfidenceLevel {
    NONE,                // No valid observation
    LOW,                 // Low confidence
    MEDIUM,              // Medium confidence
    HIGH                 // High confidence
};

/**
 * @brief Convert confidence level to string
 */
inline std::string confidence_level_to_string(ConfidenceLevel level) {
    switch (level) {
        case ConfidenceLevel::NONE: return "none";
        case ConfidenceLevel::LOW: return "low";
        case ConfidenceLevel::MEDIUM: return "medium";
        case ConfidenceLevel::HIGH: return "high";
        default: return "unknown";
    }
}

/**
 * @brief Base class for observers
 * 
 * Observers measure properties of the video signal and populate the
 * ObservationContext with their findings. They are instantiated by
 * stages that need observations (typically sinks, but also transforms
 * that require specific metadata).
 * 
 * Observers write to namespaced keys in the ObservationContext to
 * avoid collisions. Each observer declares what observations it provides
 * via get_provided_observations().
 */
class Observer {
public:
    virtual ~Observer() = default;
    
    /**
     * @brief Get observer name
     * @return Human-readable observer name (e.g., "BiphaseObserver")
     */
    virtual std::string observer_name() const = 0;
    
    /**
     * @brief Get observer version
     * @return Version string (e.g., "1.0.0")
     */
    virtual std::string observer_version() const = 0;
    
    /**
     * @brief Process a single field and populate observation context
     * 
     * Observers write their observations into the context using namespaced keys.
     * They can read previous observations from the context if needed for
     * stateful detection (e.g., field parity based on previous field).
     * 
     * @param representation Video field representation to observe
     * @param field_id Field identifier
     * @param context Observation context to populate
     */
    virtual void process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        ObservationContext& context) = 0;
    
    /**
     * @brief Declare observations this observer provides
     * 
     * Returns a list of observation keys that this observer will write
     * to the context. Used for pipeline validation and documentation.
     * 
     * @return Vector of observation keys
     */
    virtual std::vector<ObservationKey> get_provided_observations() const = 0;
    
    /**
     * @brief Get configuration schema
     * 
     * Returns parameter descriptors defining valid configuration for this observer.
     * Default implementation returns empty vector (no configuration).
     * 
     * @return Vector of parameter descriptors
     */
    virtual std::vector<ParameterDescriptor> get_configuration_schema() const {
        return {}; // Default: no configuration
    }
    
    /**
     * @brief Set configuration
     * 
     * Configuration is validated against the schema before being applied.
     * Throws std::invalid_argument if configuration is invalid.
     * 
     * @param config Configuration map
     */
    virtual void set_configuration(const std::map<std::string, ParameterValue>& config);
    
protected:
    std::map<std::string, ParameterValue> configuration_;
};

} // namespace orc
