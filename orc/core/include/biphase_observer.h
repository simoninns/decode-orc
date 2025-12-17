// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025
// 
// Observer for biphase (Manchester) encoded VBI data on lines 16-18
// Standard: IEC 60586-1986 §10.1 (PAL) / IEC 60587-1986 §10.1 (NTSC)

#ifndef ORC_CORE_BIPHASE_OBSERVER_H
#define ORC_CORE_BIPHASE_OBSERVER_H

#include "observer.h"
#include <array>
#include <optional>

namespace orc {

// CLV timecode structure
struct CLVTimecode {
    int hours;
    int minutes;
    int seconds;
    int picture_number;
};

// Observation for biphase-coded VBI data
class BiphaseObservation : public Observation {
public:
    // Raw 24-bit decoded values for lines 16, 17, 18
    // -1 = parse error, 0 = blank line, >0 = valid data
    std::array<int32_t, 3> vbi_data;
    
    // Decoded content (if present)
    std::optional<int32_t> picture_number;    // CAV frame number
    std::optional<CLVTimecode> clv_timecode;  // CLV timecode
    std::optional<int32_t> chapter_number;    // Chapter marker
    bool stop_code_present = false;           // Stop code flag
    
    std::string observation_type() const override {
        return "Biphase";
    }
};

// Observer for biphase VBI decoding
class BiphaseObserver : public Observer {
public:
    BiphaseObserver() = default;
    
    std::string observer_name() const override {
        return "BiphaseObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id) override;

private:
    // Decode a single biphase line (Manchester decoder)
    int32_t decode_manchester(const uint16_t* line_data,
                              size_t sample_count,
                              uint16_t zero_crossing,
                              size_t active_start,
                              double sample_rate);
    
    // Interpret the 3 decoded values as picture number, chapter, etc.
    void interpret_vbi_data(const std::array<int32_t, 3>& vbi_data,
                           BiphaseObservation& observation);
};

} // namespace orc

#endif // ORC_CORE_BIPHASE_OBSERVER_H
