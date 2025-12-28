/*
 * File:        vbi_utilities.cpp
 * Module:      orc-core
 * Purpose:     Vbi Utilities
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "vbi_utilities.h"
#include <algorithm>

namespace orc {
namespace vbi_utils {

std::vector<uint8_t> get_transition_map(const uint16_t* line_data,
                                         size_t sample_count,
                                         uint16_t zero_crossing) {
    // Read the data with debounce to remove transition noise (matches legacy tool)
    std::vector<uint8_t> result;
    result.reserve(sample_count);
    
    uint8_t previous_state = 0;
    uint8_t current_state = 0;
    int debounce = 0;
    
    for (size_t i = 0; i < sample_count; ++i) {
        current_state = (line_data[i] > zero_crossing) ? 1 : 0;
        
        if (current_state != previous_state) {
            debounce++;
        }
        
        if (debounce > 3) {
            debounce = 0;
            previous_state = current_state;
        }
        
        result.push_back(previous_state);
    }
    
    return result;
}

bool find_transition(const std::vector<uint8_t>& transition_map,
                     bool target_state,
                     double& position,
                     double limit) {
    size_t pos = static_cast<size_t>(position);
    if (pos >= transition_map.size() || pos >= static_cast<size_t>(limit)) {
        return false;
    }
    
    uint8_t target = target_state ? 1 : 0;
    
    // Find transition to target state
    while (pos < transition_map.size() && pos < static_cast<size_t>(limit)) {
        if (transition_map[pos] == target) {
            position = static_cast<double>(pos);
            return true;
        }
        pos++;
    }
    
    return false;
}

bool is_even_parity(uint32_t value) {
    // Count the number of 1 bits
    int count = 0;
    while (value) {
        count += value & 1;
        value >>= 1;
    }
    return (count % 2) == 0;
}

} // namespace vbi_utils
} // namespace orc
