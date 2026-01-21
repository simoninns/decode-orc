/*
 * File:        biphase_observer.cpp
 * Module:      orc-core
 * Purpose:     Biphase VBI data extraction observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "biphase_observer.h"
#include "../include/observation_context.h"
#include "../include/video_field_representation.h"
#include "../include/vbi_types.h"
#include "../include/field_id.h"
#include "../include/logging.h"
#include "../include/vbi_utilities.h"
#include <cstring>
#include <cstdio>

namespace orc {

// Decode Manchester/biphase encoded VBI data from a video line
static int32_t decode_manchester(const uint16_t* line_data,
                                 size_t sample_count,
                                 uint16_t zero_crossing,
                                 size_t active_start,
                                 double sample_rate) {
    if (!line_data || sample_count == 0) {
        return 0;
    }
    
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
        uint8_t start_state = transition_map[x];
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

// Interpret the decoded VBI data according to IEC 60857 LaserDisc standard
static void interpret_vbi_data(
    int32_t vbi16, int32_t vbi17, int32_t vbi18,
    FieldID field_id, ObservationContext& context)
{
    // IEC 60857-1986 - 10.1.3 Picture numbers (CAV discs) ----------------------------
    // Check for CAV picture number on lines 17 and 18
    // Top bit can be used for stop code, so mask it: range 0-79999
    
    if ((vbi17 & 0xF00000) == 0xF00000) {
        int32_t pic_no;
        if (decode_bcd(vbi17 & 0x07FFFF, pic_no)) {
            context.set(field_id, "vbi", "picture_number", pic_no);
            ORC_LOG_DEBUG("BiphaseObserver: CAV picture number {} from line 17", pic_no);
        }
    }
    
    if ((vbi18 & 0xF00000) == 0xF00000) {
        int32_t pic_no;
        if (decode_bcd(vbi18 & 0x07FFFF, pic_no)) {
            context.set(field_id, "vbi", "picture_number", pic_no);
            ORC_LOG_DEBUG("BiphaseObserver: CAV picture number {} from line 18", pic_no);
        }
    }
    
    // IEC 60857-1986 - 10.1.5 Chapter numbers ----------------------------------------
    // Check for chapter number on lines 17 and 18
    
    if ((vbi17 & 0xF00FFF) == 0x800DDD) {
        int32_t chapter;
        if (decode_bcd((vbi17 & 0x07F000) >> 12, chapter)) {
            context.set(field_id, "vbi", "chapter_number", chapter);
            ORC_LOG_DEBUG("BiphaseObserver: Chapter number {} from line 17", chapter);
        }
    }
    
    if ((vbi18 & 0xF00FFF) == 0x800DDD) {
        int32_t chapter;
        if (decode_bcd((vbi18 & 0x07F000) >> 12, chapter)) {
            context.set(field_id, "vbi", "chapter_number", chapter);
            ORC_LOG_DEBUG("BiphaseObserver: Chapter number {} from line 18", chapter);
        }
    }
    
    // IEC 60857-1986 - 10.1.6 Programme time code (CLV hours and minutes) -------------
    // Check for CLV programme time code on lines 17 and 18
    // Both lines should carry redundant data - verify they match
    
    CLVTimecode clv_tc{-1, -1, -1, -1};
    bool has_clv_time_line17 = false;
    bool has_clv_time_line18 = false;
    int32_t hour17 = -1, minute17 = -1;
    int32_t hour18 = -1, minute18 = -1;
    
    // Decode line 17 hours/minutes
    if ((vbi17 & 0xF0FF00) == 0xF0DD00) {
        if (decode_bcd((vbi17 & 0x0F0000) >> 16, hour17) &&
            decode_bcd(vbi17 & 0x0000FF, minute17)) {
            // Validate range: hours 0-23, minutes 0-59
            if (hour17 >= 0 && hour17 <= 23 && minute17 >= 0 && minute17 <= 59) {
                has_clv_time_line17 = true;
                ORC_LOG_DEBUG("BiphaseObserver: CLV hours={} minutes={} from line 17", hour17, minute17);
            } else {
                ORC_LOG_DEBUG("BiphaseObserver: Invalid CLV time range on line 17: {}:{}", hour17, minute17);
            }
        }
    }
    
    // Decode line 18 hours/minutes
    if ((vbi18 & 0xF0FF00) == 0xF0DD00) {
        if (decode_bcd((vbi18 & 0x0F0000) >> 16, hour18) &&
            decode_bcd(vbi18 & 0x0000FF, minute18)) {
            // Validate range: hours 0-23, minutes 0-59
            if (hour18 >= 0 && hour18 <= 23 && minute18 >= 0 && minute18 <= 59) {
                has_clv_time_line18 = true;
                ORC_LOG_DEBUG("BiphaseObserver: CLV hours={} minutes={} from line 18", hour18, minute18);
            } else {
                ORC_LOG_DEBUG("BiphaseObserver: Invalid CLV time range on line 18: {}:{}", hour18, minute18);
            }
        }
    }
    
    // Multi-line correlation: prefer matching data, warn on conflicts
    if (has_clv_time_line17 && has_clv_time_line18) {
        if (hour17 == hour18 && minute17 == minute18) {
            // Both lines agree - high confidence
            clv_tc.hours = hour17;
            clv_tc.minutes = minute17;
            ORC_LOG_DEBUG("BiphaseObserver: CLV time confirmed by both lines: {}:{}", hour17, minute17);
        } else {
            // Lines disagree - use line 17 but warn
            clv_tc.hours = hour17;
            clv_tc.minutes = minute17;
            ORC_LOG_DEBUG("BiphaseObserver: CLV time mismatch - line17={}:{} line18={}:{} (using line17)",
                         hour17, minute17, hour18, minute18);
        }
    } else if (has_clv_time_line17) {
        // Only line 17 valid
        clv_tc.hours = hour17;
        clv_tc.minutes = minute17;
    } else if (has_clv_time_line18) {
        // Only line 18 valid
        clv_tc.hours = hour18;
        clv_tc.minutes = minute18;
    }
    
    // IEC 60857-1986 - 10.1.10 CLV picture number (seconds and frame within second) ---
    // Check for CLV picture number on line 16
    // Both second and picture number must be valid
    
    if ((vbi16 & 0xF0F000) == 0x80E000) {
        int32_t sec_digit, pic_no;
        
        // First digit of second is A-F (representing 0-5 tens of seconds)
        uint32_t tens = (vbi16 & 0x0F0000) >> 16;
        
        if (tens >= 0xA && tens <= 0xF &&
            decode_bcd((vbi16 & 0x000F00) >> 8, sec_digit) &&
            decode_bcd(vbi16 & 0x0000FF, pic_no)) {
            
            int32_t seconds = (10 * (tens - 0xA)) + sec_digit;
            
            // Validate range: seconds 0-59, picture 0-29 (PAL) or 0-24 (NTSC)
            // Be permissive and accept 0-29 for both formats
            if (seconds >= 0 && seconds <= 59 && pic_no >= 0 && pic_no <= 29) {
                clv_tc.seconds = seconds;
                clv_tc.picture_number = pic_no;
                ORC_LOG_DEBUG("BiphaseObserver: CLV seconds={} picture={} from line 16", 
                             seconds, pic_no);
            } else {
                ORC_LOG_DEBUG("BiphaseObserver: Invalid CLV seconds/picture range: seconds={} picture={}", 
                             seconds, pic_no);
            }
        }
    }
    
    // Only store CLV timecode if ALL fields are present and valid
    // This ensures we have a complete, usable timecode
    if (clv_tc.hours != -1 && clv_tc.minutes != -1 && 
        clv_tc.seconds != -1 && clv_tc.picture_number != -1) {
        context.set(field_id, "vbi", "clv_timecode_hours", clv_tc.hours);
        context.set(field_id, "vbi", "clv_timecode_minutes", clv_tc.minutes);
        context.set(field_id, "vbi", "clv_timecode_seconds", clv_tc.seconds);
        context.set(field_id, "vbi", "clv_timecode_picture", clv_tc.picture_number);
        ORC_LOG_DEBUG("BiphaseObserver: Complete CLV timecode validated: {}:{}:{}.{}", 
                     clv_tc.hours, clv_tc.minutes, clv_tc.seconds, clv_tc.picture_number);
    } else if (clv_tc.hours != -1 || clv_tc.minutes != -1 || 
               clv_tc.seconds != -1 || clv_tc.picture_number != -1) {
        // Partial timecode detected but not stored - log for debugging
        ORC_LOG_DEBUG("BiphaseObserver: Incomplete CLV timecode ignored: {}:{}:{}.{}", 
                     clv_tc.hours, clv_tc.minutes, clv_tc.seconds, clv_tc.picture_number);
    }
    
    // IEC 60857-1986 - 10.1.1 Lead-in ------------------------------------------------
    if (vbi17 == 0x88FFFF || vbi18 == 0x88FFFF) {
        context.set(field_id, "vbi", "lead_in", static_cast<int32_t>(1));
        ORC_LOG_DEBUG("BiphaseObserver: Lead-in detected");
    }
    
    // IEC 60857-1986 - 10.1.2 Lead-out -----------------------------------------------
    if (vbi17 == 0x80EEEE || vbi18 == 0x80EEEE) {
        context.set(field_id, "vbi", "lead_out", static_cast<int32_t>(1));
        ORC_LOG_DEBUG("BiphaseObserver: Lead-out detected");
    }
    
    // IEC 60857-1986 - 10.1.4 Picture stop code --------------------------------------
    // Check for picture stop code on lines 16 and 17
    if (vbi16 == 0x82CFFF || vbi17 == 0x82CFFF) {
        context.set(field_id, "vbi", "stop_code_present", static_cast<int32_t>(1));
        ORC_LOG_DEBUG("BiphaseObserver: Picture stop code detected");
    }
    
    // IEC 60857-1986 - 10.1.7 Constant linear velocity code --------------------------
    // CLV indicator on line 17
    if (vbi17 == 0x87FFFF) {
        ORC_LOG_DEBUG("BiphaseObserver: CLV indicator code detected");
    }
    
    // IEC 60857-1986 - 10.1.8 Programme status code ----------------------------------
    if ((vbi16 & 0xFFF000) == 0x8DC000 || (vbi16 & 0xFFF000) == 0x8BA000) {
        // CX on or off?
        bool cx_enabled = ((vbi16 & 0x0FF000) == 0x0DC000);
        context.set(field_id, "vbi", "programme_status_cx_enabled", cx_enabled ? 1 : 0);
        
        // Extract x3, x4, x5 parameters
        uint32_t x3 = (vbi16 & 0x000F00) >> 8;
        uint32_t x4 = (vbi16 & 0x0000F0) >> 4;
        uint32_t x5 = (vbi16 & 0x00000F);
        
        // Check parity
        bool parity_valid = check_even_parity(x4, x5);
        context.set(field_id, "vbi", "programme_status_parity_valid", parity_valid ? 1 : 0);
        
        // Disc size (x31): 1=8 inch, 0=12 inch
        bool is_12_inch = ((x3 & 0x08) == 0);
        context.set(field_id, "vbi", "programme_status_is_12_inch", is_12_inch ? 1 : 0);
        
        // Disc side (x32): 1=side 2, 0=side 1
        bool is_side_1 = ((x3 & 0x04) == 0);
        context.set(field_id, "vbi", "programme_status_is_side_1", is_side_1 ? 1 : 0);
        
        // Teletext (x33): 1=present, 0=not present
        bool has_teletext = ((x3 & 0x02) != 0);
        context.set(field_id, "vbi", "programme_status_has_teletext", has_teletext ? 1 : 0);
        
        // Digital video (x42): 1=digital, 0=analogue
        bool is_digital = ((x4 & 0x04) != 0);
        context.set(field_id, "vbi", "programme_status_is_digital", is_digital ? 1 : 0);
        
        ORC_LOG_DEBUG("BiphaseObserver: Programme status - CX={}, size={}, side={}, digital={}",
                     cx_enabled, is_12_inch ? 12 : 8, is_side_1 ? 1 : 2, is_digital);
    }
    
    // IEC 60857-1986 - 10.1.9 Users code ----------------------------------------------
    if ((vbi16 & 0xF0F000) == 0x80D000) {
        uint32_t x1 = (vbi16 & 0x0F0000) >> 16;
        uint32_t x3x4x5 = (vbi16 & 0x000FFF);
        
        if (x1 <= 7) {
            // Format as hex string
            char user_code_str[8];
            snprintf(user_code_str, sizeof(user_code_str), "%01X%03X", x1, x3x4x5);
            ORC_LOG_DEBUG("BiphaseObserver: User code = {}", user_code_str);
        } else {
            ORC_LOG_DEBUG("BiphaseObserver: Invalid user code (X1 > 7)");
        }
    }
}

void BiphaseObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    ObservationContext& context)
{
    // Observers work by analyzing the rendered video data,
    // NOT by reading source metadata hints
    
    // Get the field descriptor to access video parameters
    auto descriptor = representation.get_descriptor(field_id);
    if (!descriptor) {
        ORC_LOG_TRACE("BiphaseObserver: Could not get field descriptor for field {}", field_id.value());
        return;
    }
    
    // Get video parameters for decoding
    auto video_params_opt = representation.get_video_parameters();
    if (!video_params_opt) {
        ORC_LOG_TRACE("BiphaseObserver: Could not get video parameters for field {}", field_id.value());
        return;
    }
    
    const auto& video_params = video_params_opt.value();
    
    // Calculate IRE zero-crossing point (midpoint between black and white)
    uint16_t zero_crossing = (video_params.white_16b_ire + video_params.black_16b_ire) / 2;
    size_t active_start = video_params.active_video_start;
    double sample_rate = static_cast<double>(video_params.sample_rate);
    
    // Decode lines 16, 17, 18 (VBI lines use 1-based numbering in specs, 0-based in code)
    // Lines 15, 16, 17 in 0-based indexing
    std::array<int32_t, 3> vbi_data = {0, 0, 0};
    int lines_decoded = 0;
    
    for (int line_offset = 0; line_offset < 3; ++line_offset) {
        size_t line_num = 15 + line_offset;  // Lines 15, 16, 17 (0-based)
        if (line_num >= descriptor->height) {
            vbi_data[line_offset] = -1;
            continue;
        }
        
        const uint16_t* line_data = representation.get_line(field_id, line_num);
        if (!line_data) {
            vbi_data[line_offset] = -1;
            continue;
        }
        
        vbi_data[line_offset] = decode_manchester(
            line_data, descriptor->width, zero_crossing,
            active_start, sample_rate);
        
        if (vbi_data[line_offset] != 0 && vbi_data[line_offset] != -1) {
            lines_decoded++;
        }
    }
    
    // Store the decoded VBI data in observation context
    if (lines_decoded > 0) {
        context.set(field_id, "biphase", "vbi_line_16", vbi_data[0]);
        context.set(field_id, "biphase", "vbi_line_17", vbi_data[1]);
        context.set(field_id, "biphase", "vbi_line_18", vbi_data[2]);
        
        ORC_LOG_DEBUG("BiphaseObserver: Decoded {} VBI lines for field {}: {:08x} {:08x} {:08x}",
                     lines_decoded, field_id.value(), vbi_data[0], vbi_data[1], vbi_data[2]);
        
        // Interpret the VBI data according to IEC 60857 standard
        interpret_vbi_data(vbi_data[0], vbi_data[1], vbi_data[2], field_id, context);
    } else {
        ORC_LOG_TRACE("BiphaseObserver: No biphase data decoded for field {}", field_id.value());
    }
}

} // namespace orc

