/*
 * File:        orc_video_metadata.h
 * Module:      orc-public
 * Purpose:     Public API for video metadata types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <common_types.h>  // For VideoSystem enum

namespace orc {
namespace public_api {

/**
 * @brief Video parameters from TBC metadata
 * 
 * Contains essential information about the video format, dimensions,
 * and technical parameters decoded from TBC files.
 */
struct VideoParameters {
    // Format
    VideoSystem system = VideoSystem::Unknown;
    bool is_subcarrier_locked = false;
    bool is_widescreen = false;
    
    // Field/frame dimensions
    int32_t field_width = -1;
    int32_t field_height = -1;
    int32_t number_of_sequential_fields = -1;
    
    // Field ordering
    bool is_first_field_first = true;  // True if frame N uses fields (N*2-1, N*2), false if (N*2, N*2-1)
    
    // Sample ranges
    int32_t colour_burst_start = -1;
    int32_t colour_burst_end = -1;
    int32_t active_video_start = -1;
    int32_t active_video_end = -1;
    
    // Active line ranges (field-based)
    int32_t first_active_field_line = -1;
    int32_t last_active_field_line = -1;
    
    // Active line ranges (frame-based, interlaced)
    int32_t first_active_frame_line = -1;
    int32_t last_active_frame_line = -1;
    
    // IRE levels (16-bit)
    int32_t blanking_16b_ire = -1;  // 0 IRE (blanking/pedestal level)
    int32_t black_16b_ire = -1;     // Black level (typically 7.5 IRE for NTSC, 0 IRE for PAL)
    int32_t white_16b_ire = -1;     // White level (100 IRE)
    
    // Sample rate (Hz)
    double sample_rate = -1.0;
    
    // Color subcarrier frequency (Hz)
    double fsc = -1.0;
    
    // Mapping and format
    bool is_mapped = false;
    std::string tape_format;
    
    // Source information
    std::string decoder;       // Decoder used (e.g., "ld-decode", "vhs-decode")
    std::string git_branch;
    std::string git_commit;
    
    // Active area cropping flag - when true, decoders should write to 0-based ComponentFrame
    bool active_area_cropping_applied = false;
    
    bool is_valid() const { return system != VideoSystem::Unknown && field_width > 0; }
};

} // namespace public_api
} // namespace orc
