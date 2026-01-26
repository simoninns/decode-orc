/*
 * File:        node_id.h
 * Module:      orc-common
 * Purpose:     NodeID type definition for DAG nodes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <fmt/format.h>

namespace orc {

/**
 * @brief NodeID - Unique identifier for nodes in the processing DAG
 * 
 * Uses int32_t for efficient storage and comparison. Each node in a project
 * has a unique ID assigned when it's created.
 * 
 * Properties:
 * - Unique within a project
 * - Monotonically increasing (new nodes get higher IDs)
 * - Efficient for use as map/cache keys
 * - Easy to verify and debug
 */
class NodeID {
public:
    using value_type = int32_t;
    
    // Special node ID values
    static constexpr value_type ROOT_NODE = -2;  // Virtual root node for DAG execution
    
    // Default constructor creates an invalid ID
    constexpr NodeID() noexcept : id_(-1) {}
    
    // Construct from int32_t value
    constexpr explicit NodeID(value_type id) noexcept : id_(id) {}
    
    // Named constructors for special values
    static constexpr NodeID root() noexcept { return NodeID(ROOT_NODE); }
    
    // Get the underlying value
    constexpr value_type value() const noexcept { return id_; }
    
    // Check if ID is valid (>= 0)
    constexpr bool is_valid() const noexcept { return id_ >= 0; }
    
    // Conversion
    std::string to_string() const;
    
    // Comparison operators
    constexpr bool operator==(const NodeID& other) const noexcept {
        return id_ == other.id_;
    }
    constexpr bool operator!=(const NodeID& other) const noexcept {
        return id_ != other.id_;
    }
    constexpr bool operator<(const NodeID& other) const noexcept {
        return id_ < other.id_;
    }
    constexpr bool operator<=(const NodeID& other) const noexcept {
        return id_ <= other.id_;
    }
    constexpr bool operator>(const NodeID& other) const noexcept {
        return id_ > other.id_;
    }
    constexpr bool operator>=(const NodeID& other) const noexcept {
        return id_ >= other.id_;
    }
    
private:
    value_type id_;
};

} // namespace orc

// Hash function for NodeID to use in unordered_map/unordered_set
namespace std {
    template<>
    struct hash<orc::NodeID> {
        size_t operator()(const orc::NodeID& id) const noexcept {
            return std::hash<orc::NodeID::value_type>{}(id.value());
        }
    };
}

// fmt formatter for NodeID
template <>
struct fmt::formatter<orc::NodeID> : fmt::formatter<int32_t> {
    auto format(const orc::NodeID& id, format_context& ctx) const {
        return fmt::formatter<int32_t>::format(id.value(), ctx);
    }
};
