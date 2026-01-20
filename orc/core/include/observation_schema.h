/*
 * File:        observation_schema.h
 * Module:      orc-core
 * Purpose:     Observation schema definitions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>

namespace orc {

/**
 * @brief Observation data types
 */
enum class ObservationType {
    INT32,           // 32-bit integer
    INT64,           // 64-bit integer
    DOUBLE,          // Floating point
    STRING,          // Text string
    BOOL,            // Boolean
    CUSTOM           // Custom/complex type
};

/**
 * @brief Observation key descriptor
 * 
 * Describes an observation that an observer can provide or
 * a stage can require.
 */
struct ObservationKey {
    std::string namespace_;     // Namespace (e.g., "biphase", "vitc")
    std::string name;           // Key name (e.g., "picture_number", "timecode")
    ObservationType type;       // Data type
    std::string description;    // Human-readable description
    bool optional;              // Whether observation may not be present for every field
    
    ObservationKey(const std::string& ns, 
                   const std::string& n, 
                   ObservationType t, 
                   const std::string& desc,
                   bool opt = false)
        : namespace_(ns), name(n), type(t), description(desc), optional(opt) {}
    
    // For use in containers
    bool operator<(const ObservationKey& other) const {
        if (namespace_ != other.namespace_) return namespace_ < other.namespace_;
        return name < other.name;
    }
    
    bool operator==(const ObservationKey& other) const {
        return namespace_ == other.namespace_ && name == other.name;
    }
    
    // Full key for display (namespace.name)
    std::string full_key() const {
        return namespace_ + "." + name;
    }
};

/**
 * @brief Convert observation type to string
 */
inline std::string observation_type_to_string(ObservationType type) {
    switch (type) {
        case ObservationType::INT32: return "int32";
        case ObservationType::INT64: return "int64";
        case ObservationType::DOUBLE: return "double";
        case ObservationType::STRING: return "string";
        case ObservationType::BOOL: return "bool";
        case ObservationType::CUSTOM: return "custom";
        default: return "unknown";
    }
}

} // namespace orc
