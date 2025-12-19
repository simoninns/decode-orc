/*
 * File:        closed_caption_observer.cpp
 * Module:      orc-core
 * Purpose:     Closed caption observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "closed_caption_observer.h"
#include "tbc_video_field_representation.h"
#include "vbi_utilities.h"

namespace orc {

std::vector<std::shared_ptr<Observation>> ClosedCaptionObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id) {
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<ClosedCaptionObservation>();
    observation->field_id = field_id;
    observation->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    observation->observer_version = observer_version();
    
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    // Closed captions are only on the second field (field 2) in NTSC
    // Check if this is the correct field type
    if (descriptor->format == VideoFormat::NTSC) {
        // For NTSC, captions are on line 21 of field 2 (second/even field)
        // field_id % 2 == 1 gives us the second field
        if (field_id.value() % 2 == 0) {
            // This is field 1, skip it
            observation->confidence = ConfidenceLevel::NONE;
            observations.push_back(observation);
            return observations;
        }
    }
    
    // Line 21 for NTSC, line 22 for PAL (0-based: 20, 21)
    VideoFormat format = descriptor->format;
    size_t line_num = (format == VideoFormat::NTSC) ? 20 : 21;
    
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
    
    // Get actual VideoParameters from TBCVideoFieldRepresentation
    const auto* tbc_rep = dynamic_cast<const TBCVideoFieldRepresentation*>(&representation);
    if (tbc_rep == nullptr) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    const VideoParameters& video_params = tbc_rep->video_parameters();
    
    // Zero-crossing at 25 IRE [CTA-608-E p13]
    uint16_t zero_crossing = ((video_params.white_16b_ire - video_params.black_16b_ire) / 4) + 
                             video_params.black_16b_ire;
    
    // Bit clock is 32 x fH [CTA-608-E p14]
    double samples_per_bit = static_cast<double>(descriptor->width) / 32.0;
    size_t colorburst_end = video_params.colour_burst_end;
    
    bool success = decode_line(line_data, descriptor->width,
                               zero_crossing, colorburst_end, samples_per_bit,
                               *observation);
    
    if (success) {
        observation->confidence = (observation->parity_valid[0] && observation->parity_valid[1]) ?
            ConfidenceLevel::HIGH : ConfidenceLevel::LOW;
    } else {
        observation->confidence = ConfidenceLevel::NONE;
    }
    
    observations.push_back(observation);
    return observations;
}

bool ClosedCaptionObserver::decode_line(const uint16_t* line_data,
                                       size_t sample_count,
                                       uint16_t zero_crossing,
                                       size_t colorburst_end,
                                       double samples_per_bit,
                                       ClosedCaptionObservation& observation) {
    auto transition_map = vbi_utils::get_transition_map(
        line_data, sample_count, zero_crossing);
    
    // Find 00 start bits (1.5-bit low period)
    double x = colorburst_end + (2.0 * samples_per_bit);
    double x_limit = sample_count - (17.0 * samples_per_bit);
    double last_one = x;
    
    while ((x - last_one) < (1.5 * samples_per_bit)) {
        if (x >= x_limit) return false;
        if (transition_map[static_cast<size_t>(x)]) last_one = x;
        x += 1.0;
    }
    
    // Find 1 start bit
    if (!vbi_utils::find_transition(transition_map, true, x, x_limit)) {
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
    
    observation.data0 = byte0;
    observation.data1 = byte1;
    
    // Check parity: legacy tool checks isEvenParity(byte) && parityBit != 1
    // If byte has even parity, the parity bit should be 1 to make odd parity overall
    observation.parity_valid[0] = !(vbi_utils::is_even_parity(byte0) && parity0 != 1);
    observation.parity_valid[1] = !(vbi_utils::is_even_parity(byte1) && parity1 != 1);
    
    return true;
}

} // namespace orc
