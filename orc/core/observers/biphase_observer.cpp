/*
 * File:        biphase_observer.cpp
 * Module:      orc-core
 * Purpose:     Biphase observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "biphase_observer.h"
#include "logging.h"
#include "tbc_video_field_representation.h"
#include "vbi_utilities.h"
#include <cmath>
#include <cstdio>

namespace orc {

std::vector<std::shared_ptr<Observation>> BiphaseObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    (void)history;  // Unused for now
    
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
    
    // Get video parameters using the standard interface
    auto video_params_opt = representation.get_video_parameters();
    if (!video_params_opt.has_value()) {
        observation->confidence = ConfidenceLevel::NONE;
        observations.push_back(observation);
        return observations;
    }
    
    const auto& video_params = video_params_opt.value();
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
    
    ORC_LOG_DEBUG("BiphaseObserver: Field {} VBI=[{:#08x}, {:#08x}, {:#08x}]",
                  field_id.value(), observation->vbi_data[0], 
                  observation->vbi_data[1], observation->vbi_data[2]);
    
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

// Helper function to decode BCD (Binary Coded Decimal)
static bool decode_bcd(uint32_t bcd, int32_t& output) {
    output = 0;
    int32_t multiplier = 1;
    
    while (bcd > 0) {
        uint32_t digit = bcd & 0x0F;
        if (digit > 9) {
            return false;  // Invalid BCD digit
        }
        output += digit * multiplier;
        multiplier *= 10;
        bcd >>= 4;
    }
    
    return true;
}

// Helper function to check even parity (IEC 60857-1986)
static bool check_even_parity(uint32_t x4, uint32_t x5) {
    // Count number of 1 bits in x4 and x5
    uint32_t combined = (x4 << 4) | x5;
    int count = 0;
    while (combined) {
        count += combined & 1;
        combined >>= 1;
    }
    return (count % 2) == 0;  // Even parity
}

void BiphaseObserver::interpret_vbi_data(const std::array<int32_t, 3>& vbi_data,
                                        BiphaseObservation& observation) {
    // VBI data indices: [0] = line 16, [1] = line 17, [2] = line 18
    
    int32_t vbi16 = vbi_data[0];
    int32_t vbi17 = vbi_data[1];
    int32_t vbi18 = vbi_data[2];
    
    // IEC 60857-1986 - 10.1.3 Picture numbers (CAV discs) ----------------------------
    // Check for CAV picture number on lines 17 and 18
    // Top bit can be used for stop code, so mask it: range 0-79999
    
    if ((vbi17 & 0xF00000) == 0xF00000) {
        int32_t pic_no;
        if (decode_bcd(vbi17 & 0x07FFFF, pic_no)) {
            observation.picture_number = pic_no;
            ORC_LOG_DEBUG("BiphaseObserver: CAV picture number {} from line 17", pic_no);
        }
    }
    
    if ((vbi18 & 0xF00000) == 0xF00000) {
        int32_t pic_no;
        if (decode_bcd(vbi18 & 0x07FFFF, pic_no)) {
            observation.picture_number = pic_no;
            ORC_LOG_DEBUG("BiphaseObserver: CAV picture number {} from line 18", pic_no);
        }
    }
    
    // IEC 60857-1986 - 10.1.5 Chapter numbers ----------------------------------------
    // Check for chapter number on lines 17 and 18
    
    if ((vbi17 & 0xF00FFF) == 0x800DDD) {
        int32_t chapter;
        if (decode_bcd((vbi17 & 0x07F000) >> 12, chapter)) {
            observation.chapter_number = chapter;
            ORC_LOG_DEBUG("BiphaseObserver: Chapter number {} from line 17", chapter);
        }
    }
    
    if ((vbi18 & 0xF00FFF) == 0x800DDD) {
        int32_t chapter;
        if (decode_bcd((vbi18 & 0x07F000) >> 12, chapter)) {
            observation.chapter_number = chapter;
            ORC_LOG_DEBUG("BiphaseObserver: Chapter number {} from line 18", chapter);
        }
    }
    
    // IEC 60857-1986 - 10.1.6 Programme time code (CLV hours and minutes) -------------
    // Check for CLV programme time code on lines 17 and 18
    // Both hour and minute must be valid
    
    CLVTimecode clv_tc{-1, -1, -1, -1};
    
    if ((vbi17 & 0xF0FF00) == 0xF0DD00) {
        int32_t hour, minute;
        if (decode_bcd((vbi17 & 0x0F0000) >> 16, hour) &&
            decode_bcd(vbi17 & 0x0000FF, minute)) {
            clv_tc.hours = hour;
            clv_tc.minutes = minute;
            ORC_LOG_DEBUG("BiphaseObserver: CLV hours={} minutes={} from line 17", hour, minute);
        }
    }
    
    if ((vbi18 & 0xF0FF00) == 0xF0DD00) {
        int32_t hour, minute;
        if (decode_bcd((vbi18 & 0x0F0000) >> 16, hour) &&
            decode_bcd(vbi18 & 0x0000FF, minute)) {
            clv_tc.hours = hour;
            clv_tc.minutes = minute;
            ORC_LOG_DEBUG("BiphaseObserver: CLV hours={} minutes={} from line 18", hour, minute);
        }
    }
    
    // IEC 60857-1986 - 10.1.10 CLV picture number (seconds and frame within second) ---
    // Check for CLV picture number on line 16
    // Both second and picture number must be valid
    
    if ((vbi16 & 0xF0F000) == 0x80E000) {
        int32_t sec_digit, pic_no;
        
        // First digit of second is A-F (representing 0-5 tens of seconds)
        uint32_t tens = (vbi16 & 0x0F0000) >> 16;
        
        if (tens >= 0xA &&
            decode_bcd((vbi16 & 0x000F00) >> 8, sec_digit) &&
            decode_bcd(vbi16 & 0x0000FF, pic_no)) {
            
            clv_tc.seconds = (10 * (tens - 0xA)) + sec_digit;
            clv_tc.picture_number = pic_no;
            ORC_LOG_DEBUG("BiphaseObserver: CLV seconds={} picture={} from line 16", 
                         clv_tc.seconds, clv_tc.picture_number);
        }
    }
    
    // If we have a complete CLV timecode, store it
    if (clv_tc.hours != -1 || clv_tc.minutes != -1 || 
        clv_tc.seconds != -1 || clv_tc.picture_number != -1) {
        observation.clv_timecode = clv_tc;
        ORC_LOG_DEBUG("BiphaseObserver: CLV timecode {}:{}:{}.{}", 
                     clv_tc.hours, clv_tc.minutes, clv_tc.seconds, clv_tc.picture_number);
    }
    
    // IEC 60857-1986 - 10.1.1 Lead-in ------------------------------------------------
    if (vbi17 == 0x88FFFF || vbi18 == 0x88FFFF) {
        observation.lead_in = true;
        ORC_LOG_DEBUG("BiphaseObserver: Lead-in detected");
    }
    
    // IEC 60857-1986 - 10.1.2 Lead-out -----------------------------------------------
    if (vbi17 == 0x80EEEE || vbi18 == 0x80EEEE) {
        observation.lead_out = true;
        ORC_LOG_DEBUG("BiphaseObserver: Lead-out detected");
    }
    
    // IEC 60857-1986 - 10.1.4 Picture stop code --------------------------------------
    // Check for picture stop code on lines 16 and 17
    if (vbi16 == 0x82CFFF || vbi17 == 0x82CFFF) {
        observation.stop_code_present = true;
        ORC_LOG_DEBUG("BiphaseObserver: Picture stop code detected");
    }
    
    // IEC 60857-1986 - 10.1.7 Constant linear velocity code --------------------------
    // CLV indicator on line 17
    if (vbi17 == 0x87FFFF) {
        ORC_LOG_DEBUG("BiphaseObserver: CLV indicator code detected");
    }
    
    // IEC 60857-1986 - 10.1.8 Programme status code ----------------------------------
    if ((vbi16 & 0xFFF000) == 0x8DC000 || (vbi16 & 0xFFF000) == 0x8BA000) {
        ProgrammeStatus prog_status;
        
        // CX on or off?
        prog_status.cx_enabled = ((vbi16 & 0x0FF000) == 0x0DC000);
        
        // Extract x3, x4, x5 parameters
        uint32_t x3 = (vbi16 & 0x000F00) >> 8;
        uint32_t x4 = (vbi16 & 0x0000F0) >> 4;
        uint32_t x5 = (vbi16 & 0x00000F);
        
        // Check parity
        prog_status.parity_valid = check_even_parity(x4, x5);
        
        // Disc size (x31): 1=8 inch, 0=12 inch
        prog_status.is_12_inch = ((x3 & 0x08) == 0);
        
        // Disc side (x32): 1=side 2, 0=side 1
        prog_status.is_side_1 = ((x3 & 0x04) == 0);
        
        // Teletext (x33): 1=present, 0=not present
        prog_status.has_teletext = ((x3 & 0x02) != 0);
        
        // Digital video (x42): 1=digital, 0=analogue
        prog_status.is_digital = ((x4 & 0x04) != 0);
        
        // Audio status: combination of x41, x34, x43, x44
        uint32_t audio_status = 0;
        if ((x4 & 0x08) != 0) audio_status += 8;  // x41
        if ((x3 & 0x01) != 0) audio_status += 4;  // x34
        if ((x4 & 0x02) != 0) audio_status += 2;  // x43
        if ((x4 & 0x01) != 0) audio_status += 1;  // x44
        
        // Decode audio status
        switch (audio_status) {
            case 0: prog_status.sound_mode = VbiSoundMode::STEREO; break;
            case 1: prog_status.sound_mode = VbiSoundMode::MONO; break;
            case 2: prog_status.sound_mode = VbiSoundMode::AUDIO_SUBCARRIERS_OFF; break;
            case 3: prog_status.sound_mode = VbiSoundMode::BILINGUAL; break;
            case 4: 
                prog_status.is_fm_multiplex = true;
                prog_status.sound_mode = VbiSoundMode::STEREO_STEREO;
                break;
            case 5:
                prog_status.is_fm_multiplex = true;
                prog_status.sound_mode = VbiSoundMode::STEREO_BILINGUAL;
                break;
            case 6:
                prog_status.is_fm_multiplex = true;
                prog_status.sound_mode = VbiSoundMode::CROSS_CHANNEL_STEREO;
                break;
            case 7:
                prog_status.is_fm_multiplex = true;
                prog_status.sound_mode = VbiSoundMode::BILINGUAL_BILINGUAL;
                break;
            case 8:
            case 9:
            case 11:
                prog_status.is_programme_dump = true;
                prog_status.sound_mode = VbiSoundMode::MONO_DUMP;
                break;
            case 10:
                prog_status.is_programme_dump = true;
                prog_status.sound_mode = VbiSoundMode::FUTURE_USE;
                break;
            case 12:
            case 13:
                prog_status.is_programme_dump = true;
                prog_status.is_fm_multiplex = true;
                prog_status.sound_mode = VbiSoundMode::STEREO_DUMP;
                break;
            case 14:
            case 15:
                prog_status.is_programme_dump = true;
                prog_status.is_fm_multiplex = true;
                prog_status.sound_mode = VbiSoundMode::BILINGUAL_DUMP;
                break;
        }
        
        observation.programme_status = prog_status;
        ORC_LOG_DEBUG("BiphaseObserver: Programme status - CX={}, size={}\", side={}, audio_status={}",
                     prog_status.cx_enabled, prog_status.is_12_inch ? 12 : 8, 
                     prog_status.is_side_1 ? 1 : 2, audio_status);
    }
    
    // IEC 60857-1986 - 10.1.8 Programme status (Amendment 2) -------------------------
    if ((vbi16 & 0xFFF000) == 0x8DC000 || (vbi16 & 0xFFF000) == 0x8BA000) {
        Amendment2Status am2_status;
        
        uint32_t x3 = (vbi16 & 0x000F00) >> 8;
        uint32_t x4 = (vbi16 & 0x0000F0) >> 4;
        
        // Copy permission (x34): 1=copy OK, 0=no copy
        am2_status.copy_permitted = ((x3 & 0x01) != 0);
        
        // Audio status for Am2: x41, x42, x43, x44
        uint32_t audio_status_am2 = 0;
        if ((x4 & 0x08) != 0) audio_status_am2 += 8;
        if ((x4 & 0x04) != 0) audio_status_am2 += 4;
        if ((x4 & 0x02) != 0) audio_status_am2 += 2;
        if ((x4 & 0x01) != 0) audio_status_am2 += 1;
        
        // Decode Am2 audio status
        switch (audio_status_am2) {
            case 0:
                am2_status.is_video_standard = true;
                am2_status.sound_mode = VbiSoundMode::STEREO;
                break;
            case 1:
                am2_status.is_video_standard = true;
                am2_status.sound_mode = VbiSoundMode::MONO;
                break;
            case 3:
                am2_status.is_video_standard = true;
                am2_status.sound_mode = VbiSoundMode::BILINGUAL;
                break;
            case 8:
                am2_status.is_video_standard = true;
                am2_status.sound_mode = VbiSoundMode::MONO_DUMP;
                break;
            default:
                am2_status.is_video_standard = false;
                am2_status.sound_mode = VbiSoundMode::FUTURE_USE;
                break;
        }
        
        observation.amendment2_status = am2_status;
        ORC_LOG_DEBUG("BiphaseObserver: Amendment 2 status - copy_permitted={}, video_standard={}",
                     am2_status.copy_permitted, am2_status.is_video_standard);
    }
    
    // IEC 60857-1986 - 10.1.9 Users code ----------------------------------------------
    if ((vbi16 & 0xF0F000) == 0x80D000) {
        uint32_t x1 = (vbi16 & 0x0F0000) >> 16;
        uint32_t x3x4x5 = (vbi16 & 0x000FFF);
        
        if (x1 <= 7) {
            // Format as hex string
            char user_code_str[8];
            snprintf(user_code_str, sizeof(user_code_str), "%01X%03X", x1, x3x4x5);
            observation.user_code = std::string(user_code_str);
            ORC_LOG_DEBUG("BiphaseObserver: User code = {}", observation.user_code.value());
        } else {
            ORC_LOG_DEBUG("BiphaseObserver: Invalid user code (X1 > 7)");
        }
    }
}

} // namespace orc
