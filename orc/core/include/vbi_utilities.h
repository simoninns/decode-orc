/*
 * File:        vbi_utilities.h
 * Module:      orc-core
 * Purpose:     Vbi Utilities
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_VBI_UTILITIES_H
#define ORC_CORE_VBI_UTILITIES_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orc {
namespace vbi_utils {

// Convert analog samples to binary transitions at zero-crossing point
// Returns vector where 1 = above zero-crossing, 0 = below
std::vector<uint8_t> get_transition_map(const uint16_t* line_data, 
                                         size_t sample_count,
                                         uint16_t zero_crossing);

// Find the next transition (false->true or true->false) after start position
// Updates position to the transition point
// Returns true if transition found before limit, false otherwise
bool find_transition(const std::vector<uint8_t>& transition_map,
                     bool target_state,
                     double& position,
                     double limit);

// Check if a value has even parity
bool is_even_parity(uint32_t value);

} // namespace vbi_utils
} // namespace orc

#endif // ORC_CORE_VBI_UTILITIES_H
