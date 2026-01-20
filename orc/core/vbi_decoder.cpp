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
    
    // TODO: Implement full VBI parsing logic
    // This should decode:
    // - CAV frame numbers
    // - CLV timecode
    // - Chapter numbers
    // - Control codes (stop, lead-in, lead-out)
    // - User codes
    // - Programme status
    // - Amendment 2 status
    
    // For now, just provide raw data
    ORC_LOG_DEBUG("VBIDecoder: Parsed VBI for field {} - lines: {:#06x}, {:#06x}, {:#06x}",
                  field_id.value(), vbi_line_16, vbi_line_17, vbi_line_18);
    
    return info;
}

} // namespace orc
