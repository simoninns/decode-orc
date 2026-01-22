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
    
    return parse_vbi_data(field_id, vbi_16, vbi_17, vbi_18, observation_context);
}

VBIFieldInfo VBIDecoder::parse_vbi_data(
    FieldID field_id,
    int32_t vbi_line_16,
    int32_t vbi_line_17,
    int32_t vbi_line_18,
    const ObservationContext& observation_context)
{
    VBIFieldInfo info;
    info.field_id = field_id;
    info.has_vbi_data = true;
    info.vbi_data = {vbi_line_16, vbi_line_17, vbi_line_18};
    
    // The BiphaseObserver has already interpreted the VBI data according to IEC 60857.
    // We read the interpreted values from the observation context here.
    
    // Try to get picture number
    auto pic_num_opt = observation_context.get(field_id, "vbi", "picture_number");
    if (pic_num_opt) {
        info.picture_number = std::get<int32_t>(*pic_num_opt);
    }
    
    // Try to get CLV timecode
    auto hours_opt = observation_context.get(field_id, "vbi", "clv_timecode_hours");
    auto minutes_opt = observation_context.get(field_id, "vbi", "clv_timecode_minutes");
    auto seconds_opt = observation_context.get(field_id, "vbi", "clv_timecode_seconds");
    auto picture_opt = observation_context.get(field_id, "vbi", "clv_timecode_picture");
    
    if (hours_opt && minutes_opt && seconds_opt && picture_opt) {
        CLVTimecode tc;
        tc.hours = std::get<int32_t>(*hours_opt);
        tc.minutes = std::get<int32_t>(*minutes_opt);
        tc.seconds = std::get<int32_t>(*seconds_opt);
        tc.picture_number = std::get<int32_t>(*picture_opt);
        info.clv_timecode = tc;
    }
    
    // Try to get chapter number
    auto chapter_opt = observation_context.get(field_id, "vbi", "chapter_number");
    if (chapter_opt) {
        info.chapter_number = std::get<int32_t>(*chapter_opt);
    }
    
    // Try to get control codes
    auto lead_in_opt = observation_context.get(field_id, "vbi", "lead_in");
    if (lead_in_opt) {
        info.lead_in = std::get<int32_t>(*lead_in_opt) != 0;
    }
    
    auto lead_out_opt = observation_context.get(field_id, "vbi", "lead_out");
    if (lead_out_opt) {
        info.lead_out = std::get<int32_t>(*lead_out_opt) != 0;
    }
    
    auto stop_code_opt = observation_context.get(field_id, "vbi", "stop_code_present");
    if (stop_code_opt) {
        info.stop_code_present = std::get<int32_t>(*stop_code_opt) != 0;
    }
    
    // Try to get programme status
    auto cx_opt = observation_context.get(field_id, "vbi", "programme_status_cx_enabled");
    auto size_opt = observation_context.get(field_id, "vbi", "programme_status_is_12_inch");
    auto side_opt = observation_context.get(field_id, "vbi", "programme_status_is_side_1");
    auto teletext_opt = observation_context.get(field_id, "vbi", "programme_status_has_teletext");
    auto digital_opt = observation_context.get(field_id, "vbi", "programme_status_is_digital");
    auto parity_opt = observation_context.get(field_id, "vbi", "programme_status_parity_valid");
    
    if (cx_opt || size_opt || side_opt || teletext_opt || digital_opt || parity_opt) {
        ProgrammeStatus prog_status;
        if (cx_opt) prog_status.cx_enabled = std::get<int32_t>(*cx_opt) != 0;
        if (size_opt) prog_status.is_12_inch = std::get<int32_t>(*size_opt) != 0;
        if (side_opt) prog_status.is_side_1 = std::get<int32_t>(*side_opt) != 0;
        if (teletext_opt) prog_status.has_teletext = std::get<int32_t>(*teletext_opt) != 0;
        if (digital_opt) prog_status.is_digital = std::get<int32_t>(*digital_opt) != 0;
        if (parity_opt) prog_status.parity_valid = std::get<int32_t>(*parity_opt) != 0;
        info.programme_status = prog_status;
    }
    
    ORC_LOG_DEBUG("VBIDecoder: Parsed VBI for field {} - lines: {:#08x}, {:#08x}, {:#08x}",
                  field_id.value(), vbi_line_16, vbi_line_17, vbi_line_18);
    
    return info;
}

VBIFieldInfo VBIDecoder::merge_frame_vbi(
    const VBIFieldInfo& field1_info,
    const VBIFieldInfo& field2_info)
{
    VBIFieldInfo merged;
    
    // Use first field ID as base
    merged.field_id = field1_info.field_id;
    
    // Has VBI data if either field has it
    merged.has_vbi_data = field1_info.has_vbi_data || field2_info.has_vbi_data;
    
    // Raw VBI data - prefer first field, use second as fallback
    merged.vbi_data = field1_info.has_vbi_data ? field1_info.vbi_data : field2_info.vbi_data;
    
    // Picture number - use whichever field has it (prefer first)
    if (field1_info.picture_number.has_value()) {
        merged.picture_number = field1_info.picture_number;
    } else if (field2_info.picture_number.has_value()) {
        merged.picture_number = field2_info.picture_number;
    }
    
    // CLV timecode - merge components from both fields
    // Hours/minutes may be on one field, seconds/picture on another
    bool has_hours = false, has_minutes = false, has_seconds = false, has_picture = false;
    CLVTimecode merged_tc;
    
    if (field1_info.clv_timecode.has_value()) {
        const auto& tc1 = field1_info.clv_timecode.value();
        merged_tc = tc1;
        has_hours = has_minutes = has_seconds = has_picture = true;
    }
    
    if (field2_info.clv_timecode.has_value()) {
        const auto& tc2 = field2_info.clv_timecode.value();
        if (!has_hours) { merged_tc.hours = tc2.hours; has_hours = true; }
        if (!has_minutes) { merged_tc.minutes = tc2.minutes; has_minutes = true; }
        if (!has_seconds) { merged_tc.seconds = tc2.seconds; has_seconds = true; }
        if (!has_picture) { merged_tc.picture_number = tc2.picture_number; has_picture = true; }
    }
    
    if (has_hours && has_minutes && has_seconds && has_picture) {
        merged.clv_timecode = merged_tc;
    }
    
    // Chapter number - use whichever field has it (prefer first)
    if (field1_info.chapter_number.has_value()) {
        merged.chapter_number = field1_info.chapter_number;
    } else if (field2_info.chapter_number.has_value()) {
        merged.chapter_number = field2_info.chapter_number;
    }
    
    // User code - prefer first field
    if (field1_info.user_code.has_value()) {
        merged.user_code = field1_info.user_code;
    } else if (field2_info.user_code.has_value()) {
        merged.user_code = field2_info.user_code;
    }
    
    // Control codes - OR together from both fields
    merged.lead_in = field1_info.lead_in || field2_info.lead_in;
    merged.lead_out = field1_info.lead_out || field2_info.lead_out;
    merged.stop_code_present = field1_info.stop_code_present || field2_info.stop_code_present;
    
    // Programme status - prefer first field
    if (field1_info.programme_status.has_value()) {
        merged.programme_status = field1_info.programme_status;
    } else if (field2_info.programme_status.has_value()) {
        merged.programme_status = field2_info.programme_status;
    }
    
    ORC_LOG_DEBUG("VBIDecoder: Merged frame VBI from fields {} and {}", 
                  field1_info.field_id.value(), field2_info.field_id.value());
    
    return merged;
}

} // namespace orc
