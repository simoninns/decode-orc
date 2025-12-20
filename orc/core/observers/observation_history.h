/*
 * File:        observation_history.h
 * Module:      orc-core
 * Purpose:     Observation history for observers that need previous field data
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "observer.h"
#include "field_id.h"
#include <memory>
#include <vector>
#include <map>
#include <optional>

namespace orc {

/**
 * @brief Provides access to observations from previous fields
 * 
 * This allows observers to access results from earlier in the processing sequence,
 * enabling stateful detection algorithms (like field parity) without making
 * observers themselves stateful.
 * 
 * The history is populated by the execution engine (e.g., LD sink) as fields
 * are processed. It is field_id-based, so it handles out-of-order field processing
 * correctly (e.g., after field reordering by field map stages).
 * 
 * **Caching and Refreshing:**
 * - History can be pre-populated from input metadata/hints when available
 * - New observations overwrite cached ones as fields are re-processed
 * - clear() should be called when starting a new processing run
 */
class ObservationHistory {
public:
    ObservationHistory() = default;
    
    /**
     * @brief Add observations for a field
     * 
     * This is called by the execution engine after processing each field.
     * 
     * @param field_id The field identifier
     * @param observations Vector of observations from all observers
     */
    void add_observations(FieldID field_id, 
                         const std::vector<std::shared_ptr<Observation>>& observations);
    
    /**
     * @brief Get all observations for a specific field
     * 
     * @param field_id The field to query
     * @return Vector of all observations for that field (empty if field not processed)
     */
    std::vector<std::shared_ptr<Observation>> get_observations(FieldID field_id) const;
    
    /**
     * @brief Get observations of a specific type for a field
     * 
     * @param field_id The field to query
     * @param observation_type The observation type name (e.g., "FieldParity")
     * @return Vector of matching observations (empty if none found)
     */
    std::vector<std::shared_ptr<Observation>> get_observations(
        FieldID field_id, 
        const std::string& observation_type) const;
    
    /**
     * @brief Get a single observation of a specific type for a field
     * 
     * Convenience method for observers that expect exactly one observation
     * of a given type per field.
     * 
     * @param field_id The field to query
     * @param observation_type The observation type name
     * @return The observation, or nullptr if not found
     */
    std::shared_ptr<Observation> get_observation(
        FieldID field_id,
        const std::string& observation_type) const;
    
    /**
     * @brief Check if observations exist for a field
     * 
     * @param field_id The field to check
     * @return true if any observations exist for this field
     */
    bool has_field(FieldID field_id) const;
    
    /**
     * @brief Get the most recent field ID that has been processed
     * 
     * @return The latest field ID, or invalid FieldID if no fields processed
     */
    FieldID get_latest_field() const;
    
    /**
     * @brief Clear all history
     * 
     * Used to reset state between processing runs.
     */
    void clear();
    
private:
    // Map from field_id to all observations for that field
    std::map<FieldID, std::vector<std::shared_ptr<Observation>>> history_;
};

} // namespace orc
