/*
 * File:        biphase_observer.h
 * Module:      orc-core
 * Purpose:     Biphase observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

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

// Sound mode enumeration (IEC 60857-1986)
enum class VbiSoundMode {
    STEREO = 0,
    MONO = 1,
    AUDIO_SUBCARRIERS_OFF = 2,
    BILINGUAL = 3,
    STEREO_STEREO = 4,
    STEREO_BILINGUAL = 5,
    CROSS_CHANNEL_STEREO = 6,
    BILINGUAL_BILINGUAL = 7,
    MONO_DUMP = 8,
    STEREO_DUMP = 9,
    BILINGUAL_DUMP = 10,
    FUTURE_USE = 11
};

// Programme status information
struct ProgrammeStatus {
    bool cx_enabled = false;          // CX noise reduction on/off
    bool is_12_inch = true;           // Disc size: true=12", false=8"
    bool is_side_1 = true;            // Disc side
    bool has_teletext = false;        // Teletext present
    bool is_digital = false;          // Digital vs analogue video
    VbiSoundMode sound_mode = VbiSoundMode::STEREO;
    bool is_fm_multiplex = false;     // FM-FM multiplex
    bool is_programme_dump = false;   // Programme dump mode
    bool parity_valid = false;        // Parity check passed
};

// Amendment 2 programme status
struct Amendment2Status {
    bool copy_permitted = false;      // Copy permission flag
    bool is_video_standard = false;   // Video signal standard
    VbiSoundMode sound_mode = VbiSoundMode::STEREO;
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
    bool stop_code_present = false;           // Picture stop code (0x82CFFF)
    bool lead_in = false;                     // Lead-in code (0x88FFFF)
    bool lead_out = false;                    // Lead-out code (0x80EEEE)
    std::optional<std::string> user_code;     // User code string
    std::optional<ProgrammeStatus> programme_status;   // Programme status (original)
    std::optional<Amendment2Status> amendment2_status; // Programme status (Amendment 2)
    
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
        FieldID field_id,
        const ObservationHistory& history) override;

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
