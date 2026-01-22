/*
 * File:        closed_caption_observer.cpp
 * Module:      orc-core
 * Purpose:     Closed caption observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "closed_caption_observer.h"
#include "logging.h"
#include "tbc_video_field_representation.h"
#include "vbi_utilities.h"
#include "video_field_representation.h"
#include "observation_context.h"

namespace orc {

void ClosedCaptionObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    ObservationContext& context) {
    
    ORC_LOG_DEBUG("ClosedCaptionObserver::process_field called for field {}", field_id.value());
    
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        ORC_LOG_DEBUG("Field {}: No descriptor available", field_id.value());
        context.set(field_id, "closed_caption", "present", false);
        return;
    }
    
    // Closed captions are only on the second field (field 2) in NTSC
    // Check if this is the correct field type
    if (descriptor->format == VideoFormat::NTSC) {
        // For NTSC, captions are on line 21 of field 2 (even field_id values: 2, 4, 6...)
        // field_id % 2 == 1 means odd field_id (field 1 type), so skip it
        if (field_id.value() % 2 == 1) {
            // This is field 1 type, skip it
            ORC_LOG_DEBUG("Field {}: Skipping odd field (field 1 type)", field_id.value());
            context.set(field_id, "closed_caption", "present", false);
            return;
        }
        ORC_LOG_DEBUG("Field {}: Processing even field (field 2 type)", field_id.value());
    }
    
    // Line 21 for NTSC, line 22 for PAL (0-based: 20, 21)
    VideoFormat format = descriptor->format;
    size_t line_num = (format == VideoFormat::NTSC) ? 20 : 21;
    
    if (line_num >= descriptor->height) {
        ORC_LOG_DEBUG("Field {}: Line {} >= height {}", field_id.value(), line_num, descriptor->height);
        context.set(field_id, "closed_caption", "present", false);
        return;
    }
    
    const uint16_t* line_data = representation.get_line(field_id, line_num);
    if (line_data == nullptr) {
        ORC_LOG_DEBUG("Field {}: get_line({}) returned nullptr", field_id.value(), line_num);
        context.set(field_id, "closed_caption", "present", false);
        return;
    }
    
    // Debug: check if line data has any non-zero values
    if (field_id.value() < 10) {
        std::string sample_debug;
        for (size_t i = 100; i < 130 && i < descriptor->width; i += 5) {
            sample_debug += std::to_string(line_data[i]) + " ";
        }
        ORC_LOG_DEBUG("Field {}: Line {} samples[100-130 step 5]: {}", 
                      field_id.value(), line_num, sample_debug);
    }
    
    // Get actual VideoParameters from TBCVideoFieldRepresentation
    // Need to unwrap through VideoFieldRepresentationWrapper chain
    const TBCVideoFieldRepresentation* tbc_rep = nullptr;
    const VideoFieldRepresentation* current = &representation;
    
    // Try direct cast first
    tbc_rep = dynamic_cast<const TBCVideoFieldRepresentation*>(current);
    
    // If not direct TBC, unwrap through wrapper chain
    while (tbc_rep == nullptr && current != nullptr) {
        auto* wrapper = dynamic_cast<const VideoFieldRepresentationWrapper*>(current);
        if (wrapper) {
            current = wrapper->get_source().get();
            if (current) {
                tbc_rep = dynamic_cast<const TBCVideoFieldRepresentation*>(current);
            }
        } else {
            break;
        }
    }
    
    if (tbc_rep == nullptr) {
        ORC_LOG_DEBUG("Field {}: Failed to find TBCVideoFieldRepresentation in wrapper chain", field_id.value());
        context.set(field_id, "closed_caption", "present", false);
        return;
    }
    
    auto video_params_opt = tbc_rep->get_video_parameters();
    if (!video_params_opt.has_value()) {
        ORC_LOG_DEBUG("Field {}: Failed to get video parameters", field_id.value());
        context.set(field_id, "closed_caption", "present", false);
        return;
    }
    
    const VideoParameters& video_params = video_params_opt.value();
    
    // Calculate zero crossing from actual line data
    // VBI line amplitude may differ from active video, so use line's own min/max
    uint16_t min_sample = 65535, max_sample = 0;
    for (size_t i = 0; i < descriptor->width; ++i) {
        if (line_data[i] < min_sample) min_sample = line_data[i];
        if (line_data[i] > max_sample) max_sample = line_data[i];
    }
    
    // Use midpoint of actual line samples (like BiphaseObserver does)
    uint16_t zero_crossing = (min_sample + max_sample) / 2;
    
    if (field_id.value() < 3) {
        ORC_LOG_DEBUG("Field {}: Line {} min={}, max={}, zero_crossing={}", 
                      field_id.value(), line_num, min_sample, max_sample, zero_crossing);
    }
    
    // Bit clock is 32 x fH [CTA-608-E p14]
    double samples_per_bit = static_cast<double>(descriptor->width) / 32.0;
    size_t colorburst_end = video_params.colour_burst_end;
    
    DecodedCaption decoded;
    bool success = decode_line(line_data, descriptor->width,
                               zero_crossing, colorburst_end, samples_per_bit,
                               decoded);
    
    context.set(field_id, "closed_caption", "present", success);
    if (success) {
        context.set(field_id, "closed_caption", "data0", static_cast<int32_t>(decoded.data0));
        context.set(field_id, "closed_caption", "data1", static_cast<int32_t>(decoded.data1));
        context.set(field_id, "closed_caption", "parity0_valid", decoded.parity_valid0);
        context.set(field_id, "closed_caption", "parity1_valid", decoded.parity_valid1);
        
        ORC_LOG_DEBUG("ClosedCaptionObserver: Field {} CC=[{:#04x}, {:#04x}] parity=({}, {})",
                      field_id.value(), decoded.data0, decoded.data1,
                      decoded.parity_valid0, decoded.parity_valid1);
    }
}

std::vector<ObservationKey> ClosedCaptionObserver::get_provided_observations() const {
    return {
        {"closed_caption", "present", ObservationType::BOOL, "Closed caption data decoded", true},
        {"closed_caption", "data0", ObservationType::INT32, "First EIA-608 byte (7-bit + parity)", true},
        {"closed_caption", "data1", ObservationType::INT32, "Second EIA-608 byte (7-bit + parity)", true},
        {"closed_caption", "parity0_valid", ObservationType::BOOL, "Parity validity for first byte", true},
        {"closed_caption", "parity1_valid", ObservationType::BOOL, "Parity validity for second byte", true},
    };
}

bool ClosedCaptionObserver::decode_line(const uint16_t* line_data,
                                        size_t sample_count,
                                        uint16_t zero_crossing,
                                        size_t colorburst_end,
                                        double samples_per_bit,
                                        DecodedCaption& decoded) const {
    ORC_LOG_DEBUG("decode_line: sample_count={}, zero_crossing={}, colorburst_end={}, samples_per_bit={}",
                  sample_count, zero_crossing, colorburst_end, samples_per_bit);
    
    auto transition_map = vbi_utils::get_transition_map(
        line_data, sample_count, zero_crossing);
    
    ORC_LOG_DEBUG("decode_line: transition_map size={}", transition_map.size());
    
    // Find 00 start bits (1.5-bit low period)
    double x = colorburst_end + (2.0 * samples_per_bit);
    double x_limit = sample_count - (17.0 * samples_per_bit);
    double last_one = x;
    
    while ((x - last_one) < (1.5 * samples_per_bit)) {
        if (x >= x_limit) {
            ORC_LOG_DEBUG("decode_line: Failed to find 00 start bits (x={}, x_limit={})", x, x_limit);
            return false;
        }
        if (transition_map[static_cast<size_t>(x)]) last_one = x;
        x += 1.0;
    }
    
    // Find 1 start bit
    double x_before_search = x;
    if (!vbi_utils::find_transition(transition_map, true, x, x_limit)) {
        // Debug: show transition map around the search position
        ORC_LOG_DEBUG("decode_line: Failed to find 1 start bit at x={}", x_before_search);
        size_t start_pos = std::max(0, static_cast<int>(x_before_search) - 10);
        size_t end_pos = std::min(transition_map.size(), static_cast<size_t>(x_before_search) + 50);
        std::string map_debug;
        for (size_t i = start_pos; i < end_pos; ++i) {
            map_debug += (transition_map[i] ? '1' : '0');
        }
        ORC_LOG_DEBUG("decode_line: transition_map[{}-{}]: {}", start_pos, end_pos-1, map_debug);
        return false;
    }
    
    // Skip start bit, move to first data bit
    x += 1.5 * samples_per_bit;
    
    // Decode first byte (7 bits + parity)
    uint8_t byte0 = 0;
    for (int i = 0; i < 7; ++i) {
        byte0 >>= 1;
        if (transition_map[static_cast<size_t>(x)]) byte0 += 64;
        x += samples_per_bit;
    }
    uint8_t parity0 = transition_map[static_cast<size_t>(x)] ? 1 : 0;
    x += samples_per_bit;
    
    // Decode second byte
    uint8_t byte1 = 0;
    for (int i = 0; i < 7; ++i) {
        byte1 >>= 1;
        if (transition_map[static_cast<size_t>(x)]) byte1 += 64;
        x += samples_per_bit;
    }
    uint8_t parity1 = transition_map[static_cast<size_t>(x)] ? 1 : 0;
    
    decoded.data0 = byte0;
    decoded.data1 = byte1;
    
    // Check parity: legacy tool checks isEvenParity(byte) && parityBit != 1
    // If byte has even parity, the parity bit should be 1 to make odd parity overall
    decoded.parity_valid0 = !(vbi_utils::is_even_parity(byte0) && parity0 != 1);
    decoded.parity_valid1 = !(vbi_utils::is_even_parity(byte1) && parity1 != 1);
    
    return true;
}

} // namespace orc
