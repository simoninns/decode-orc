/*
* File:        observation_context_interface.h
 * Module:      orc-core
 * Purpose:     Pipeline-scoped observation storage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DECODE_ORC_ROOT_OBSERVATION_CONTEXT_INTERFACE_H
#define DECODE_ORC_ROOT_OBSERVATION_CONTEXT_INTERFACE_H

#include <field_id.h>
#include <string>
#include <map>
#include <vector>
#include <variant>
#include <optional>
#include <cstdint>

#include "observation_schema.h"

namespace orc
{
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
     * @brief Interface for ObservationContext.
     *
     * Purpose: To increase encapsulation and make mocking within unit tests easier
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
    class IObservationContext {
    public:
        virtual ~IObservationContext() = default;

        /**
         * @brief Set an observation value for a specific field
         *
         * @param field_id Field identifier
         * @param namespace_ Namespace (typically observer type, e.g., "biphase", "vitc")
         * @param key Observation key (e.g., "picture_number", "timecode")
         * @param value Observation value
         */
        virtual void set(FieldID field_id,
                         const std::string& namespace_,
                         const std::string& key,
                         const orc::ObservationValue& value) = 0;

        /**
         * @brief Get an observation value for a specific field
         *
         * @param field_id Field identifier
         * @param namespace_ Namespace
         * @param key Observation key
         * @return Observation value if present, std::nullopt otherwise
         */
        virtual std::optional<ObservationValue> get(FieldID field_id,
                                                    const std::string& namespace_,
                                                    const std::string& key) const = 0;

        /**
         * @brief Check if an observation exists for a specific field
         *
         * @param field_id Field identifier
         * @param namespace_ Namespace
         * @param key Observation key
         * @return True if observation exists, false otherwise
         */
        virtual bool has(FieldID field_id,
                         const std::string& namespace_,
                         const std::string& key) const = 0;

        /**
         * @brief Get all observation keys for a field in a namespace
         *
         * @param field_id Field identifier
         * @param namespace_ Namespace
         * @return Vector of observation keys
         */
        virtual std::vector<std::string> get_keys(FieldID field_id,
                                                  const std::string& namespace_) const = 0;

        /**
         * @brief Get all namespaces that have observations for a field
         *
         * @param field_id Field identifier
         * @return Vector of namespace names
         */
        virtual std::vector<std::string> get_namespaces(FieldID field_id) const = 0;

        /**
         * @brief Get all observations for a specific field
         *
         * @param field_id Field identifier
         * @return Map of namespace -> (key -> value)
         */
        virtual std::map<std::string, std::map<std::string, ObservationValue>>
        get_all_observations(FieldID field_id) const = 0;

        /**
         * @brief Clear all observations
         *
         * Should be called when starting a new processing run.
         */
        virtual void clear() = 0;

        /**
         * @brief Clear observations for a specific field
         *
         * @param field_id Field identifier
         */
        virtual void clear_field(FieldID field_id) = 0;

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
        virtual void register_schema(const std::vector<ObservationKey>& keys) = 0;

        /**
         * @brief Clear all registered schema entries
         */
        virtual void clear_schema() = 0;
    };
}

#endif //DECODE_ORC_ROOT_OBSERVATION_CONTEXT_INTERFACE_H