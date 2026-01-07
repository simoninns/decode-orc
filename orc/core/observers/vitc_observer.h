/*
 * File:        vitc_observer.h
 * Module:      orc-core
 * Purpose:     VITC observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_VITC_OBSERVER_H
#define ORC_CORE_VITC_OBSERVER_H

#include "observer.h"
#include <array>
#include <vector>

namespace orc {

// Observation for VITC timecode data
class VitcObservation : public Observation {
public:
    std::array<uint8_t, 8> vitc_data;  // 8 bytes of decoded VITC data
    size_t line_number = 0;             // Which line was successfully decoded
    
    // Decoded timecode fields
    uint8_t hours = 0;
    uint8_t minutes = 0;
    uint8_t seconds = 0;
    uint8_t frames = 0;
    bool color_frame_flag = false;
    bool drop_frame_flag = false;
    std::array<uint8_t, 8> user_bits;
    
    std::string observation_type() const override {
        return "VITC";
    }
};

// Observer for VITC timecode decoding
class VitcObserver : public Observer {
public:
    VitcObserver() = default;
    
    std::string observer_name() const override {
        return "VitcObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) override;

private:
    // Get priority-ordered list of lines to try for VITC
    std::vector<size_t> get_line_numbers(VideoFormat format);
    
    // Try to decode VITC from a single line
    bool decode_line(const uint16_t* line_data,
                    size_t sample_count,
                    uint16_t zero_crossing,
                    size_t colorburst_end,
                    double samples_per_bit,
                    VitcObservation& observation);
    
    // Parse decoded VITC bytes into timecode fields
    void parse_vitc_data(VitcObservation& observation);
};

} // namespace orc

#endif // ORC_CORE_VITC_OBSERVER_H
