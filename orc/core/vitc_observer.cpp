/*
 * File:        vitc_observer.cpp
 * Module:      orc-core
 * Purpose:     VITC observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "vitc_observer.h"
#include "tbc_video_field_representation.h"
#include "vbi_utilities.h"
#include <cmath>

namespace orc {

std::vector<std::shared_ptr<Observation>> VitcObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id) {
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<VitcObservation>();
    observation->field_id = field_id;
    observation->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    observation->observer_version = observer_version();
    observation->vitc_data = {0, 0, 0, 0, 0, 0, 0, 0};
    observation->user_bits = {0, 0, 0, 0, 0, 0, 0, 0};
    
    // Get field descriptor
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Get format to determine which lines to try
    VideoFormat format = descriptor->format;
    auto line_numbers = get_line_numbers(format);
    
    // VITC parameters
    // Zero-crossing at 40 IRE
    uint16_t white_ire = 50000;
    uint16_t black_ire = 15000;
    uint16_t zero_crossing = black_ire + 
        ((40 * (white_ire - black_ire)) / 100);
    
    // Bit rate is field_width / 115
    double samples_per_bit = descriptor->width / 115.0;
    
    // Colorburst end (rough estimate)
    size_t colorburst_end = descriptor->width / 10;
    
    // Try each line in priority order
    bool found = false;
    for (size_t line_num : line_numbers) {
        if (line_num >= descriptor->height) {
            continue;
        }
        
        const uint16_t* line_data = representation.get_line(field_id, line_num);
        if (line_data == nullptr) {
            continue;
        }
        
        if (decode_line(line_data, descriptor->width, zero_crossing,
                       colorburst_end, samples_per_bit, *observation)) {
            observation->line_number = line_num;
            found = true;
            break;
        }
    }
    
    if (found) {
        parse_vitc_data(*observation);
        observation->confidence = (observation->line_number == line_numbers[0]) ?
            ConfidenceLevel::HIGH : ConfidenceLevel::MEDIUM;
    } else {
        observation->confidence = ConfidenceLevel::NONE;
    }
    
    observations.push_back(observation);
    return observations;
}

std::vector<size_t> VitcObserver::get_line_numbers(VideoFormat format) {
    // Return priority-ordered list of lines to try
    // Standards recommend specific lines, try those first
    if (format == VideoFormat::PAL) {
        // PAL lines 6-22, prioritize 19 (doesn't clash with LaserDisc VBI)
        return {18, 17, 19, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 
                20, 21};
    } else {
        // NTSC lines 10-20, prioritize 14 (doesn't clash with LaserDisc VBI)
        return {13, 14, 12, 15, 11, 16, 10, 17, 18, 19};
    }
}

bool VitcObserver::decode_line(const uint16_t* line_data,
                               size_t sample_count,
                               uint16_t zero_crossing,
                               size_t colorburst_end,
                               double samples_per_bit,
                               VitcObservation& observation) {
    // Convert to binary
    auto data_bits = vbi_utils::get_transition_map(
        line_data, sample_count, zero_crossing);
    
    // Find leading edge of first byte
    double byte_start = static_cast<double>(colorburst_end);
    double byte_start_limit = sample_count - (90 * samples_per_bit);
    
    // Find 0->1 transition
    if (!vbi_utils::find_transition(data_bits, false, byte_start, byte_start_limit)) {
        return false;
    }
    if (!vbi_utils::find_transition(data_bits, true, byte_start, byte_start_limit)) {
        return false;
    }
    
    // Decode 9 bytes (8 data + 1 CRC)
    std::array<int32_t, 9> vitc_bytes = {0};
    std::array<int32_t, 12> crc_bytes = {0};
    int bit_count = 0;
    
    for (int byte_num = 0; byte_num < 9; ++byte_num) {
        // Resynchronize on 1->0 transition
        byte_start += samples_per_bit * 0.5;
        byte_start_limit += 10 * samples_per_bit;
        if (!vbi_utils::find_transition(data_bits, false, byte_start, byte_start_limit)) {
            return false;
        }
        byte_start -= samples_per_bit;
        
        // Extract 10 bits (LSB first)
        for (int i = 0; i < 10; ++i) {
            size_t sample_pos = static_cast<size_t>(byte_start + ((i + 0.5) * samples_per_bit));
            if (sample_pos >= data_bits.size()) {
                return false;
            }
            
            int bit = data_bits[sample_pos] ? 1 : 0;
            vitc_bytes[byte_num] |= (bit << i);
            
            // Accumulate for CRC
            crc_bytes[bit_count / 8] |= (bit << (bit_count % 8));
            bit_count++;
        }
        
        // Check sync bits (should be 01 = 1 in binary)
        if ((vitc_bytes[byte_num] & 3) != 1) {
            return false;
        }
        
        // Remove sync bits
        vitc_bytes[byte_num] >>= 2;
        
        byte_start += 10.0 * samples_per_bit;
    }
    
    // Validate CRC (XOR all bytes should be 0)
    int32_t crc_total = 0;
    for (int32_t val : crc_bytes) {
        crc_total ^= val;
    }
    if (crc_total != 0) {
        return false;
    }
    
    // Copy to observation (first 8 bytes are data, 9th is CRC)
    for (int i = 0; i < 8; ++i) {
        observation.vitc_data[i] = static_cast<uint8_t>(vitc_bytes[i]);
    }
    
    return true;
}

void VitcObserver::parse_vitc_data(VitcObservation& observation) {
    // Parse VITC data according to SMPTE ST 12-1:2008
    // Time code is in BCD format
    
    // Frames (bytes 0-1)
    observation.frames = ((observation.vitc_data[0] & 0x0F)) +
                        ((observation.vitc_data[0] & 0x30) >> 4) * 10;
    
    // Seconds (bytes 2-3)
    observation.seconds = ((observation.vitc_data[1] & 0x0F)) +
                         ((observation.vitc_data[1] & 0x70) >> 4) * 10;
    
    // Minutes (bytes 4-5)
    observation.minutes = ((observation.vitc_data[2] & 0x0F)) +
                         ((observation.vitc_data[2] & 0x70) >> 4) * 10;
    
    // Hours (bytes 6-7)
    observation.hours = ((observation.vitc_data[3] & 0x0F)) +
                       ((observation.vitc_data[3] & 0x30) >> 4) * 10;
    
    // Flags
    observation.drop_frame_flag = (observation.vitc_data[0] & 0x40) != 0;
    observation.color_frame_flag = (observation.vitc_data[0] & 0x80) != 0;
    
    // User bits (4 bits from each of the first 8 bytes)
    for (int i = 0; i < 8; ++i) {
        observation.user_bits[i] = (observation.vitc_data[i] & 0x0F);
    }
}

} // namespace orc
