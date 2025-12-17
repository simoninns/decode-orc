// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025
// 
// Observer for biphase (Manchester) encoded VBI data

#include "biphase_observer.h"
#include "tbc_video_field_representation.h"
#include "vbi_utilities.h"
#include <cmath>

namespace orc {

std::vector<std::shared_ptr<Observation>> BiphaseObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id) {
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<BiphaseObservation>();
    observation->field_id = field_id;
    observation->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    observation->observer_version = observer_version();
    observation->vbi_data = {0, 0, 0};
    
    // Get field descriptor
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Get video parameters from TBC representation
    auto* tbc_rep = dynamic_cast<const TBCVideoFieldRepresentation*>(&representation);
    if (!tbc_rep) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    const auto& video_params = tbc_rep->video_parameters();
    if (!video_params.is_valid()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Calculate IRE zero-crossing point
    uint16_t zero_crossing = (video_params.white_16b_ire + video_params.black_16b_ire) / 2;
    size_t active_start = video_params.active_video_start;
    double sample_rate = video_params.sample_rate;
    
    // Decode lines 16, 17, 18 (1-based line numbers in specs, 0-based in code)
    int lines_decoded = 0;
    for (int line_offset = 0; line_offset < 3; ++line_offset) {
        size_t line_num = 15 + line_offset;  // Lines 15, 16, 17 (0-based)
        if (line_num >= descriptor->height) {
            continue;
        }
        
        const uint16_t* line_data = representation.get_line(field_id, line_num);
        if (line_data == nullptr) {
            observation->vbi_data[line_offset] = -1;
            continue;
        }
        
        observation->vbi_data[line_offset] = decode_manchester(
            line_data, descriptor->width, zero_crossing,
            active_start, sample_rate);
        
        if (observation->vbi_data[line_offset] != 0 && 
            observation->vbi_data[line_offset] != -1) {
            lines_decoded++;
        }
    }
    
    // Set confidence based on number of lines successfully decoded
    if (lines_decoded == 3) {
        observation->confidence = ConfidenceLevel::HIGH;
    } else if (lines_decoded > 0) {
        observation->confidence = ConfidenceLevel::MEDIUM;
    } else {
        observation->confidence = ConfidenceLevel::NONE;
    }
    
    // Interpret the VBI data
    if (lines_decoded > 0) {
        interpret_vbi_data(observation->vbi_data, *observation);
    }
    
    observations.push_back(observation);
    return observations;
}

int32_t BiphaseObserver::decode_manchester(const uint16_t* line_data,
                                           size_t sample_count,
                                           uint16_t zero_crossing,
                                           size_t active_start,
                                           double sample_rate) {
    // Get transition map
    auto transition_map = vbi_utils::get_transition_map(
        line_data, sample_count, zero_crossing);
    
    // Calculate samples for 1.5us (cell window is 2us, we jump 1.5us)
    double jump_samples = (sample_rate / 1000000.0) * 1.5;
    
    // Find first transition
    size_t x = active_start;
    while (x < transition_map.size() && !transition_map[x]) {
        x++;
    }
    
    if (x >= transition_map.size()) {
        return 0;  // No data
    }
    
    // First transition is always 01 in Manchester code
    int32_t result = 1;
    int decode_count = 1;
    
    // Decode remaining bits
    while (x < transition_map.size() && decode_count < 24) {
        x += static_cast<size_t>(jump_samples);
        if (x >= transition_map.size()) {
            break;
        }
        
        // Find next transition from current position
        bool start_state = transition_map[x];
        while (x < transition_map.size() && transition_map[x] == start_state) {
            x++;
        }
        
        if (x >= transition_map.size()) {
            break;
        }
        
        // Check transition direction
        if (!transition_map[x - 1] && transition_map[x]) {
            // 01 transition = 1
            result = (result << 1) | 1;
        } else {
            // 10 transition = 0
            result = result << 1;
        }
        decode_count++;
    }
    
    // Must have exactly 24 bits
    if (decode_count != 24) {
        return (decode_count == 0) ? 0 : -1;  // 0 = blank, -1 = error
    }
    
    return result;
}

void BiphaseObserver::interpret_vbi_data(const std::array<int32_t, 3>& vbi_data,
                                        BiphaseObservation& observation) {
    // Check for picture numbers (commonly on line 1, bits indicate frame number)
    // This is a simplified interpretation - full implementation would parse
    // the specific bit patterns according to IEC standards
    
    // Picture number detection (CAV discs)
    // Format: 0xf80000 | frame_number (bits 0-17 are frame, bit 18-23 are control)
    for (int32_t data : vbi_data) {
        if (data > 0) {
            // Check for CAV picture number pattern (0x80000 bit set)
            if ((data & 0x800000) != 0) {
                int32_t frame = data & 0x7FFFF;  // Lower 19 bits
                if (frame > 0 && frame < 80000) {  // Valid CAV range
                    observation.picture_number = frame;
                    break;
                }
            }
            
            // Check for CLV timecode pattern (0xF00000 bits indicate timecode)
            if ((data & 0xF00000) == 0xF00000) {
                // CLV timecode encoding (simplified)
                // Full implementation would parse BCD digits
                // For now, just mark as present
                // TODO: Full CLV timecode parsing
            }
            
            // Check for chapter marker (0x8D bits pattern)
            if ((data & 0xFF0000) == 0x8D0000) {
                observation.chapter_number = (data & 0x00FF);
            }
            
            // Check for stop code (0x8E pattern)
            if ((data & 0xFF0000) == 0x8E0000) {
                observation.stop_code_present = true;
            }
        }
    }
}

} // namespace orc
