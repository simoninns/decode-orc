/*
 * File:        fm_code_observer.cpp
 * Module:      orc-core
 * Purpose:     FM code observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "fm_code_observer.h"
#include "logging.h"
#include "tbc_video_field_representation.h"
#include "vbi_utilities.h"

namespace orc {

std::vector<std::shared_ptr<Observation>> FmCodeObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    (void)history;  // Unused
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<FmCodeObservation>();
    observation->field_id = field_id;
    observation->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    observation->observer_version = observer_version();
    
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Only for NTSC
    if (descriptor->format != VideoFormat::NTSC) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Line 10 (0-based: 9)
    size_t line_num = 9;
    if (line_num >= descriptor->height) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    const uint16_t* line_data = representation.get_line(field_id, line_num);
    if (line_data == nullptr) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    uint16_t white_ire = 50000;
    uint16_t black_ire = 15000;
    uint16_t zero_crossing = (white_ire + black_ire) / 2;
    
    // 0.75us per bit at ~40MHz sample rate
    double jump_samples = (descriptor->width / 32.0) * 0.75;
    size_t active_start = descriptor->width / 8;
    
    bool success = decode_line(line_data, descriptor->width,
                               zero_crossing, active_start, jump_samples,
                               *observation);
    
    observation->confidence = success ? ConfidenceLevel::HIGH : ConfidenceLevel::NONE;
    
    ORC_LOG_DEBUG("FmCodeObserver: Field {} fm_code={:#06x}",
                  field_id.value(), observation->data_value);
    
    observations.push_back(observation);
    return observations;
}

bool FmCodeObserver::decode_line(const uint16_t* line_data,
                                 size_t sample_count,
                                 uint16_t zero_crossing,
                                 size_t active_start,
                                 double jump_samples,
                                 FmCodeObservation& observation) {
    auto fm_data = vbi_utils::get_transition_map(
        line_data, sample_count, zero_crossing);
    
    // Find first transition
    size_t x = active_start;
    while (x < fm_data.size() && !fm_data[x]) {
        x++;
    }
    if (x >= fm_data.size()) return false;
    
    uint64_t decoded_bytes = 0;
    int decode_count = 0;
    size_t last_transition_x = x;
    bool last_state = fm_data[x];
    
    // Decode 40 bits
    while (x < fm_data.size() && decode_count < 40) {
        while (x < fm_data.size() && fm_data[x] == last_state) {
            x++;
        }
        if (x >= fm_data.size()) break;
        
        last_state = fm_data[x];
        
        // Transition in middle of cell = 1, otherwise 0
        if (x - last_transition_x < static_cast<size_t>(jump_samples)) {
            decoded_bytes = (decoded_bytes << 1) | 1;
            last_transition_x = x;
            decode_count++;
            
            while (x < fm_data.size() && fm_data[x] == last_state) {
                x++;
            }
            if (x >= fm_data.size()) break;
            last_state = fm_data[x];
            last_transition_x = x;
        } else {
            decoded_bytes = decoded_bytes << 1;
            last_transition_x = x;
            decode_count++;
        }
        x++;
    }
    
    if (decode_count != 40) return false;
    
    // Parse 40-bit structure
    uint64_t clock_sync = (decoded_bytes & 0xF000000000ULL) >> 36;
    uint64_t field_indicator = (decoded_bytes & 0x0800000000ULL) >> 35;
    uint64_t leading_sync = (decoded_bytes & 0x07F0000000ULL) >> 28;
    uint64_t data_value = (decoded_bytes & 0x000FFFFF00ULL) >> 8;
    uint64_t parity_bit = (decoded_bytes & 0x0000000080ULL) >> 7;
    uint64_t trailing_sync = (decoded_bytes & 0x000000007FULL);
    
    // Validate sync patterns
    if (clock_sync != 3 || leading_sync != 114 || trailing_sync != 13) {
        return false;
    }
    
    // Check parity
    bool data_even_parity = vbi_utils::is_even_parity(static_cast<uint32_t>(data_value));
    if (parity_bit == 1 && !data_even_parity) return false;
    if (parity_bit == 0 && data_even_parity) return false;
    
    observation.data_value = static_cast<uint32_t>(data_value);
    observation.field_flag = (field_indicator != 0);
    
    return true;
}

} // namespace orc
