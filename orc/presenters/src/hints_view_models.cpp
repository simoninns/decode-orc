/*
 * File:        hints_view_models.cpp
 * Module:      orc-presenters
 * Purpose:     Implementation of hint view model conversion functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/hints_view_models.h"
#include <orc_video_metadata.h>  // For public API VideoParameters
#include <common_types.h>  // For VideoSystem enum

namespace orc::presenters {

VideoParametersView toVideoParametersView(const orc::public_api::VideoParameters& params) {
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

} // namespace orc::presenters
