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

} // namespace orc
