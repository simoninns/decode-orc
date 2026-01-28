/*
 * File:        hints_view_models.cpp
 * Module:      orc-presenters
 * Purpose:     Implementation of hint view model conversion functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/hints_view_models.h"
#include <orc_source_parameters.h>
#include <common_types.h>  // For VideoSystem enum

namespace orc::presenters {

VideoParametersView toVideoParametersView(const orc::SourceParameters& params) {
    VideoParametersView v{};
    
    // Convert VideoSystem enum
    switch (params.system) {
        case orc::VideoSystem::PAL:
            v.system = VideoSystem::PAL;
            break;
        case orc::VideoSystem::NTSC:
            v.system = VideoSystem::NTSC;
            break;
        case orc::VideoSystem::PAL_M:
            v.system = VideoSystem::PAL_M;
            break;
        default:
            v.system = VideoSystem::Unknown;
            break;
    }
    
    // Copy all fields
    v.field_width = params.field_width;
    v.field_height = params.field_height;
    v.active_video_start = params.active_video_start;
    v.active_video_end = params.active_video_end;
    v.color_burst_start = params.colour_burst_start;
    v.color_burst_end = params.colour_burst_end;
    v.white_ire = params.white_16b_ire;
    v.blanking_ire = params.blanking_16b_ire;
    v.black_ire = params.black_16b_ire;
    v.sample_rate = params.sample_rate;
    
    return v;
}

std::optional<FieldToFrameDisplayResult> fieldToFrameCoordinates(
    VideoSystem system,
    uint64_t field_index,
    int field_line_number)
{
    if (field_line_number < 1) {
        return std::nullopt;  // Invalid line number
    }

    // Determine if this is the first or second field
    bool is_first_field = (field_index % 2) == 0;
    
    // Calculate frame number (1-based)
    uint64_t frame_number = (field_index / 2) + 1;
    
    // Get field heights based on system and parity
    size_t first_field_height = 0;
    switch (system) {
        case VideoSystem::NTSC:
        case VideoSystem::PAL_M:
            // NTSC: First field = 262 lines, Second field = 263 lines
            first_field_height = 262;
            break;
        case VideoSystem::PAL:
            // PAL: First field = 312 lines, Second field = 313 lines
            first_field_height = 312;
            break;
        case VideoSystem::Unknown:
        default:
            return std::nullopt;  // Unknown system
    }
    
    // Calculate frame line number based on field parity
    int frame_line_number;
    if (is_first_field) {
        // First field: frame lines start at 1
        frame_line_number = field_line_number;
    } else {
        // Second field: frame lines are offset by first field height
        frame_line_number = field_line_number + static_cast<int>(first_field_height);
    }
    
    return FieldToFrameDisplayResult{frame_number, frame_line_number, is_first_field};
}

} // namespace orc::presenters
