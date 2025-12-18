/******************************************************************************
 * field_id.h
 *
 * FieldID - The fundamental coordinate system for ld-decode-orc
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>

namespace orc {

/**
 * @brief FieldID - The fundamental coordinate system for decode-orc
 * 
 * Represents a monotonic sequence derived from input TBC capture order.
 * Properties:
 * - Unique and strictly ordered
 * - Not timestamps
 * - Not assumed to be uniformly spaced
 * - Fields may be missing, duplicated, or discontinuous
 * - Represents capture order, not playback time
 * 
 * All time-varying data (video, PCM, EFM, metadata) is associated with
 * one or more FieldIDs.
 */
class FieldID {
public:
    using value_type = uint64_t;
    
    // Special values
    static constexpr value_type INVALID = std::numeric_limits<value_type>::max();
    static constexpr value_type MIN_VALID = 0;
    
    // Constructors
    constexpr FieldID() noexcept : value_(INVALID) {}
    constexpr explicit FieldID(value_type value) noexcept : value_(value) {}
    
    // Accessors
    constexpr value_type value() const noexcept { return value_; }
    constexpr bool is_valid() const noexcept { return value_ != INVALID; }
    
    // Conversion
    std::string to_string() const;
    
    // Comparison operators
    constexpr bool operator==(const FieldID& other) const noexcept {
        return value_ == other.value_;
    }
    constexpr bool operator!=(const FieldID& other) const noexcept {
        return value_ != other.value_;
    }
    constexpr bool operator<(const FieldID& other) const noexcept {
        return value_ < other.value_;
    }
    constexpr bool operator<=(const FieldID& other) const noexcept {
        return value_ <= other.value_;
    }
    constexpr bool operator>(const FieldID& other) const noexcept {
        return value_ > other.value_;
    }
    constexpr bool operator>=(const FieldID& other) const noexcept {
        return value_ >= other.value_;
    }
    
    // Arithmetic (for iteration, ranges)
    constexpr FieldID operator+(value_type offset) const noexcept {
        return FieldID(value_ + offset);
    }
    constexpr FieldID operator-(value_type offset) const noexcept {
        return FieldID(value_ - offset);
    }
    constexpr value_type operator-(const FieldID& other) const noexcept {
        return value_ - other.value_;
    }
    
    FieldID& operator++() noexcept {
        ++value_;
        return *this;
    }
    FieldID operator++(int) noexcept {
        FieldID tmp(*this);
        ++value_;
        return tmp;
    }

private:
    value_type value_;
};

/**
 * @brief Represents a continuous range of FieldIDs [start, end)
 */
struct FieldIDRange {
    FieldID start;
    FieldID end;  // exclusive
    
    constexpr FieldIDRange() noexcept = default;
    constexpr FieldIDRange(FieldID s, FieldID e) noexcept : start(s), end(e) {}
    
    constexpr bool contains(FieldID id) const noexcept {
        return id >= start && id < end;
    }
    
    constexpr bool is_valid() const noexcept {
        return start.is_valid() && end.is_valid() && start < end;
    }
    
    constexpr FieldID::value_type size() const noexcept {
        return end.value() - start.value();
    }
};

} // namespace orc

// Hash support for std::unordered_map, etc.
namespace std {
    template<>
    struct hash<orc::FieldID> {
        size_t operator()(const orc::FieldID& id) const noexcept {
            return hash<orc::FieldID::value_type>{}(id.value());
        }
    };
}
