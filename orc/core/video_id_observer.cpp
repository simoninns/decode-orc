// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "video_id_observer.h"
#include "tbc_video_field_representation.h"
#include "vbi_utilities.h"

namespace orc {

std::vector<std::shared_ptr<Observation>> VideoIdObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id) {
    
    std::vector<std::shared_ptr<Observation>> observations;
    auto observation = std::make_shared<VideoIdObservation>();
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
    
    // Line 20 (0-based: 19)
    size_t line_num = 19;
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
    
    // Zero-crossing at 35 IRE
    uint16_t white_ire = 50000;
    uint16_t black_ire = 15000;
    uint16_t zero_crossing = ((white_ire - black_ire) * 35 / 100) + black_ire;
    
    // Bit clock is fSC / 8, approximately field_width * 16 / 455
    double samples_per_bit = descriptor->width * 16.0 / 455.0;
    size_t colorburst_end = descriptor->width / 10;
    
    bool success = decode_line(line_data, descriptor->width,
                               zero_crossing, colorburst_end, samples_per_bit,
                               *observation);
    
    observation->confidence = success ? ConfidenceLevel::HIGH : ConfidenceLevel::NONE;
    observations.push_back(observation);
    return observations;
}

bool VideoIdObserver::decode_line(const uint16_t* line_data,
                                  size_t sample_count,
                                  uint16_t zero_crossing,
                                  size_t colorburst_end,
                                  double samples_per_bit,
                                  VideoIdObservation& observation) {
    auto transition_map = vbi_utils::get_transition_map(
        line_data, sample_count, zero_crossing);
    
    double x = colorburst_end;
    double x_limit = sample_count - (22.0 * samples_per_bit);
    
    // Find start bits (10)
    if (!vbi_utils::find_transition(transition_map, true, x, x_limit)) {
        return false;
    }
    x += samples_per_bit * 1.5;
    if (transition_map[static_cast<size_t>(x)]) {
        return false;  // Should be 0
    }
    
    // Decode 20-bit codeword
    uint32_t codeword = 0;
    x += samples_per_bit;
    for (int i = 0; i < 20; ++i) {
        codeword = (codeword << 1) + (transition_map[static_cast<size_t>(x)] ? 1 : 0);
        x += samples_per_bit;
    }
    
    // Extract fields
    uint32_t word0 = (codeword & 0xC0000) >> 18;
    uint32_t word1 = (codeword & 0x3C000) >> 14;
    uint32_t word2 = (codeword & 0x03F80) >> 7;
    uint32_t crcc = codeword & 0x3F;
    uint32_t message = codeword >> 6;
    
    // Calculate CRC-6 (x^6 + x + 1, init 0x3F)
    uint32_t crc = 0x3F;
    for (int i = 0; i < 14; ++i) {
        int invert = ((message >> i) & 1) ^ ((crc >> 5) & 1);
        crc ^= invert;
        crc = (crc << 1) | invert;
    }
    crc &= 0x3F;
    
    if (crc != crcc) {
        return false;
    }
    
    observation.video_id_data = static_cast<uint16_t>(message);
    observation.word0 = static_cast<uint8_t>(word0);
    observation.word1 = static_cast<uint8_t>(word1);
    observation.word2 = static_cast<uint8_t>(word2);
    
    return true;
}

} // namespace orc
