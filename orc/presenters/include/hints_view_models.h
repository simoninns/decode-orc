/*
 * File:        hints_view_models.h
 * Module:      orc-presenters
 * Purpose:     View-facing hint data models for GUI/CLI layers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>

namespace orc::presenters {

/**
 * @brief Source of a hint as exposed to presentation layers.
 */
enum class HintSourceView {
    METADATA,
    USER_OVERRIDE,
    INHERITED,
    SAMPLE_ANALYSIS,
    CORROBORATED,
    UNKNOWN
};

struct FieldParityHintView {
    bool is_first_field = false;
    HintSourceView source = HintSourceView::UNKNOWN;
    int confidence_pct = 0;
};

struct FieldPhaseHintView {
    int field_phase_id = -1;   // -1 means unknown
    HintSourceView source = HintSourceView::UNKNOWN;
    int confidence_pct = 0;
};

struct ActiveLineHintView {
    int first_active_frame_line = -1;
    int last_active_frame_line = -1;
    HintSourceView source = HintSourceView::UNKNOWN;
    int confidence_pct = 0;

    bool is_valid() const {
        return first_active_frame_line >= 0 &&
               last_active_frame_line >= first_active_frame_line;
    }
};

/**
 * @brief Video system/format enumeration for presenter layer
 */
enum class VideoSystem {
    PAL,    // 625-line PAL
    NTSC,   // 525-line NTSC
    PAL_M,  // 525-line PAL
    Unknown
};

/**
 * @brief Video parameters view model for presenter layer
 * 
 * Contains all video format and timing parameters needed by GUI.
 * Mirrors core VideoParameters but in presenter layer.
 */
struct VideoParametersView {
    // Format
    VideoSystem system = VideoSystem::Unknown;
    
    // Field/frame dimensions
    int field_width = -1;
    int field_height = -1;
    
    // Sample ranges
    int color_burst_start = -1;
    int color_burst_end = -1;
    int active_video_start = -1;
    int active_video_end = -1;
    
    // IRE levels (16-bit)
    int white_ire = -1;      // White level (100 IRE)
    int black_ire = -1;      // Black level
    int blanking_ire = -1;   // Blanking/pedestal level (0 IRE)
    
    // Sample rate (Hz)
    double sample_rate = 0.0;
};

} // namespace orc::presenters

// Include public API types that are used
#include <orc_source_parameters.h>

namespace orc::presenters {

/**
 * @brief Convert core SourceParameters to presenter VideoParametersView
 * 
 * This helper function encapsulates the conversion logic to avoid
 * duplication across the codebase.
 */
VideoParametersView toVideoParametersView(const orc::SourceParameters& params);

} // namespace orc::presenters
