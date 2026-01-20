/*
 * File:        observation_context.h
 * Module:      orc-core
 * Purpose:     Pipeline-scoped observation storage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "field_id.h"
#include <string>
#include <map>
#include <vector>
#include <variant>
#include <optional>
#include <cstdint>
#include "observation_schema.h"

namespace orc {

/**
 * @brief Type-safe observation value
 * 
 * Observations can be various types depending on what is being measured.
 * This variant covers common observation data types.
 */
using ObservationValue = std::variant<
    int32_t,           // Integer values (e.g., picture number, chapter)
    int64_t,           // Large integer values (e.g., field sequence numbers)
    double,            // Floating point values (e.g., burst level, SNR)
    std::string,       // String values (e.g., timecode, text, confidence levels)
    bool               // Boolean values (e.g., flag present/absent)
>;

/**
 * @brief Pipeline-scoped observation storage
 * 
 * ObservationContext stores typed, namespaced observations collected
 * throughout pipeline execution. It flows alongside the VFR through
 * all stages.
 * 
 * Namespaces prevent collisions between different observer types.
 * Keys within a namespace identify specific data fields.
 * 
 * Observations are stored per-field to support field-level metadata.
 * 
 * @example
 * ObservationContext context;
 * context.set(field_id, "biphase", "picture_number", 12345);
 * auto pn = context.get(field_id, "biphase", "picture_number");
 * if (pn && std::holds_alternative<int32_t>(*pn)) {
 *     int32_t picture_number = std::get<int32_t>(*pn);
 * }
 */
class ObservationContext {
public:
    ObservationContext() = default;
    
    /**
     * @brief Set an observation value for a specific field
     * 
     * @param field_id Field identifier
     * @param namespace_ Namespace (typically observer type, e.g., "biphase", "vitc")
     * @param key Observation key (e.g., "picture_number", "timecode")
     * @param value Observation value
     */
    void set(FieldID field_id, 
             const std::string& namespace_, 
             const std::string& key, 
             const ObservationValue& value);
    
    /**
     * @brief Get an observation value for a specific field
     * 
     * @param field_id Field identifier
     * @param namespace_ Namespace
     * @param key Observation key
     * @return Observation value if present, std::nullopt otherwise
     */
    std::optional<ObservationValue> get(FieldID field_id,
                                        const std::string& namespace_, 
                                        const std::string& key) const;
    
    /**
     * @brief Check if an observation exists for a specific field
     * 
     * @param field_id Field identifier
     * @param namespace_ Namespace
     * @param key Observation key
     * @return True if observation exists, false otherwise
     */
    bool has(FieldID field_id,
             const std::string& namespace_, 
             const std::string& key) const;
    
    /**
     * @brief Get all observation keys for a field in a namespace
     * 
     * @param field_id Field identifier
     * @param namespace_ Namespace
     * @return Vector of observation keys
     */
    std::vector<std::string> get_keys(FieldID field_id,
                                      const std::string& namespace_) const;
    
    /**
     * @brief Get all namespaces that have observations for a field
     * 
     * @param field_id Field identifier
     * @return Vector of namespace names
     */
    std::vector<std::string> get_namespaces(FieldID field_id) const;
    
    /**
     * @brief Get all observations for a specific field
     * 
     * @param field_id Field identifier
     * @return Map of namespace -> (key -> value)
     */
    std::map<std::string, std::map<std::string, ObservationValue>> 
    get_all_observations(FieldID field_id) const;
    
    /**
     * @brief Clear all observations
     * 
     * Should be called when starting a new processing run.
     */
    void clear();
    
    /**
     * @brief Clear observations for a specific field
     * 
     * @param field_id Field identifier
     */
    void clear_field(FieldID field_id);

    /**
     * @brief Register observation schema entries to enable type validation
     * 
     * Stages should declare provided observations; the executor may
     * aggregate and register them prior to execution. When a schema
     * is registered, subsequent set() calls will be validated against
     * the expected types. Unknown keys are allowed (to permit exploratory
     * data), but if a key exists in the schema and the type mismatches,
     * set() will throw std::invalid_argument.
     */
    void register_schema(const std::vector<ObservationKey>& keys);

    /**
     * @brief Clear all registered schema entries
     */
    void clear_schema();

private:
    // Storage: field_id -> namespace -> key -> value
    std::map<FieldID, std::map<std::string, std::map<std::string, ObservationValue>>> observations_;

    // Schema: (namespace,name) -> expected ObservationType
    std::map<std::pair<std::string, std::string>, ObservationType> schema_;

    static bool value_matches_type(const ObservationValue& v, ObservationType t);
};

} // namespace orc
