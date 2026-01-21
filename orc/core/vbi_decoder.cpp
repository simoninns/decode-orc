/*
 * File:        vbi_decoder.cpp
 * Module:      orc-core
 * Purpose:     VBI decoding API implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "vbi_decoder.h"
#include "include/observation_context.h"
#include "logging.h"

namespace orc {

std::optional<VBIFieldInfo> VBIDecoder::decode_vbi(
    const ObservationContext& observation_context,
    FieldID field_id)
{
    // Try to get VBI observations from the biphase namespace
    auto vbi_16_opt = observation_context.get(field_id, "biphase", "vbi_line_16");
    auto vbi_17_opt = observation_context.get(field_id, "biphase", "vbi_line_17");
    auto vbi_18_opt = observation_context.get(field_id, "biphase", "vbi_line_18");
    
    if (!vbi_16_opt || !vbi_17_opt || !vbi_18_opt) {
        ORC_LOG_DEBUG("VBIDecoder: No VBI data found for field {}", field_id.value());
        VBIFieldInfo info;
        info.field_id = field_id;
        info.has_vbi_data = false;
        info.vbi_data = {0, 0, 0};
        info.error_message = "No VBI data available";
        return info;
    }
    
    // Extract int32_t values from variant
    int32_t vbi_16 = std::get<int32_t>(*vbi_16_opt);
    int32_t vbi_17 = std::get<int32_t>(*vbi_17_opt);
    int32_t vbi_18 = std::get<int32_t>(*vbi_18_opt);
    
    return parse_vbi_data(field_id, vbi_16, vbi_17, vbi_18);
}

VBIFieldInfo VBIDecoder::parse_vbi_data(
    FieldID field_id,
    int32_t vbi_line_16,
    int32_t vbi_line_17,
    int32_t vbi_line_18)
{
    VBIFieldInfo info;
    info.field_id = field_id;
    info.has_vbi_data = true;
    info.vbi_data = {vbi_line_16, vbi_line_17, vbi_line_18};
    
    // VBI data is encoded in biphase format across lines 16-18
    // Line 16 (0x00000F): Contains CAV picture number or CLV timecode info
    // Line 17 (0x000F00): Contains chapter/control information
    // Line 18 (0x0F0000): Contains programme status and other info
    
    // Extract biphase data from each line (removing framing bits)
    // Each 16-bit word contains 14 data bits with biphase encoding
    
    // Line 16: Picture number (CAV) or timecode (CLV)
    if (vbi_line_16 != 0) {
        // Extract the biphase-encoded data (bits 0-13)
        uint16_t line16_data = static_cast<uint16_t>(vbi_line_16 & 0x3FFF);
        
        // Try to decode as CAV picture number first
        // CAV mode: bits 0-13 contain picture number (14 bits = up to 16384)
        int32_t picture_num = static_cast<int32_t>(line16_data & 0x3FFF);
        if (picture_num > 0 && picture_num < 100000) {
            info.picture_number = picture_num;
            ORC_LOG_DEBUG("VBIDecoder: Extracted CAV picture number: {}", picture_num);
        } else {
            // Try CLV timecode decoding (VITC format)
            // Line 16 contains frame and seconds information in BCD
            // Bits 0-3: Frame tens
            // Bits 4-7: Frame units
            // Bits 8-11: Seconds tens
            // Bits 12-15: Seconds units
            
            int frames = ((line16_data >> 0) & 0x0F) * 10 + ((line16_data >> 4) & 0x0F);
            int seconds = ((line16_data >> 8) & 0x0F) * 10 + ((line16_data >> 12) & 0x0F);
            
            // Validate ranges
            if (frames >= 0 && frames < 30 && seconds >= 0 && seconds < 60) {
                CLVTimecode tc;
                tc.picture_number = frames;
                tc.seconds = seconds;
                tc.minutes = -1;  // Need to get from other lines
                tc.hours = -1;
                
                // Store tentative timecode - will be completed by other lines if available
                info.clv_timecode = tc;
                ORC_LOG_DEBUG("VBIDecoder: Extracted CLV timecode frame/sec: {}/{}", frames, seconds);
            }
        }
    }
    
    // Line 17: Chapter/control codes and timecode continuation
    if (vbi_line_17 != 0) {
        uint16_t line17_data = static_cast<uint16_t>(vbi_line_17 & 0x3FFF);
        
        // Extract minutes and hours for CLV timecode
        // Bits 0-3: Minutes tens (in BCD)
        // Bits 4-7: Minutes units (in BCD)
        // Bits 8-11: Hours tens (in BCD)
        // Bits 12-15: Hours units (in BCD) + control bits
        
        int minutes = ((line17_data >> 0) & 0x0F) * 10 + ((line17_data >> 4) & 0x0F);
        int hours = ((line17_data >> 8) & 0x0F) * 10 + ((line17_data >> 12) & 0x0F);
        
        // Update CLV timecode if it exists
        if (info.clv_timecode.has_value()) {
            CLVTimecode tc = info.clv_timecode.value();
            if (minutes >= 0 && minutes < 60) {
                tc.minutes = minutes;
            }
            if (hours >= 0 && hours < 24) {
                tc.hours = hours;
            }
            info.clv_timecode = tc;
            ORC_LOG_DEBUG("VBIDecoder: Updated CLV timecode HH:MM: {:02d}:{:02d}", hours, minutes);
        }
        
        // Extract chapter number (bits 0-5 contain chapter in some implementations)
        int32_t chapter = (line17_data >> 8) & 0x3F;
        if (chapter > 0 && chapter < 64) {
            info.chapter_number = chapter;
            ORC_LOG_DEBUG("VBIDecoder: Extracted chapter number: {}", chapter);
        }
        
        // Extract control codes (high bits)
        uint8_t control = (line17_data >> 10) & 0x3F;
        
        // Control code bit patterns (IEC 60857):
        // Bit 13: Lead-in marker
        // Bit 12: Lead-out marker  
        // Bit 11: Picture stop code
        if (control & 0x08) {  // Bit 13
            info.lead_in = true;
            ORC_LOG_DEBUG("VBIDecoder: Lead-in detected");
        }
        if (control & 0x04) {  // Bit 12
            info.lead_out = true;
            ORC_LOG_DEBUG("VBIDecoder: Lead-out detected");
        }
        if (control & 0x02) {  // Bit 11
            info.stop_code_present = true;
            ORC_LOG_DEBUG("VBIDecoder: Stop code detected");
        }
    }
    
    // Line 18: Programme status and other metadata
    if (vbi_line_18 != 0) {
        uint16_t line18_data = static_cast<uint16_t>(vbi_line_18 & 0x3FFF);
        
        // Extract programme status bits
        ProgrammeStatus prog_status;
        
        // Bit layout (typical IEC 60857):
        // Bit 0: CX enabled
        // Bit 1: Disc size (0=12", 1=8")
        // Bit 2: Side (0=side1, 1=side2)
        // Bit 3: Teletext present
        // Bit 4: Digital video
        // Bits 5-7: Sound mode
        // Bit 8: FM multiplex
        // Bit 9: Programme dump
        
        prog_status.cx_enabled = (line18_data & 0x0001) != 0;
        prog_status.is_12_inch = (line18_data & 0x0002) == 0;
        prog_status.is_side_1 = (line18_data & 0x0004) == 0;
        prog_status.has_teletext = (line18_data & 0x0008) != 0;
        prog_status.is_digital = (line18_data & 0x0010) != 0;
        
        // Extract sound mode (3 bits: bits 5-7)
        uint8_t sound_mode = (line18_data >> 5) & 0x07;
        if (sound_mode < 8) {
            prog_status.sound_mode = static_cast<VbiSoundMode>(sound_mode);
        }
        
        prog_status.is_fm_multiplex = (line18_data & 0x0100) != 0;
        prog_status.is_programme_dump = (line18_data & 0x0200) != 0;
        prog_status.parity_valid = (line18_data & 0x2000) != 0;
        
        info.programme_status = prog_status;
        ORC_LOG_DEBUG("VBIDecoder: Extracted programme status: CX={}, 12\"={}, Side1={}, Teletext={}, Digital={}",
                     prog_status.cx_enabled, prog_status.is_12_inch, prog_status.is_side_1,
                     prog_status.has_teletext, prog_status.is_digital);
    }
    
    ORC_LOG_DEBUG("VBIDecoder: Parsed VBI for field {} - lines: {:#06x}, {:#06x}, {:#06x}",
                  field_id.value(), vbi_line_16, vbi_line_17, vbi_line_18);
    
    return info;
}

} // namespace orc
