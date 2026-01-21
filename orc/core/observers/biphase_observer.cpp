/*
 * File:        biphase_observer.cpp
 * Module:      orc-core
 * Purpose:     Biphase VBI data extraction observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "biphase_observer.h"
#include "include/observation_context.h"
#include "include/video_field_representation.h"
#include "include/vbi_types.h"
#include "include/field_id.h"
#include "logging.h"

namespace orc {

void BiphaseObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    ObservationContext& context)
{
    // Try to get VBI data from field representation
    // The field representation should provide access to VBI data via metadata or observations
    
    // Get VBI hint if available (from TBC metadata)
    auto vbi_hint = representation.get_vbi_hint(field_id);
    
    if (vbi_hint) {
        // Populate raw VBI lines
        context.set(field_id, "biphase", "vbi_line_16", static_cast<int32_t>(vbi_hint->vbi_data[0]));
        context.set(field_id, "biphase", "vbi_line_17", static_cast<int32_t>(vbi_hint->vbi_data[1]));
        context.set(field_id, "biphase", "vbi_line_18", static_cast<int32_t>(vbi_hint->vbi_data[2]));
        
        ORC_LOG_DEBUG("BiphaseObserver: Extracted VBI data for field {}: {:08x} {:08x} {:08x}",
                     field_id.value(),
                     vbi_hint->vbi_data[0],
                     vbi_hint->vbi_data[1],
                     vbi_hint->vbi_data[2]);
        
        // Extract decoded data from hint if available
        // The hint structure may contain pre-decoded information
        // For now, we just extract the raw data - full decoding happens in VBIDecoder
        
    } else {
        ORC_LOG_TRACE("BiphaseObserver: No VBI data available for field {}", field_id.value());
    }
}

} // namespace orc

