/*
 * File:        active_line_hint.h
 * Module:      orc-core/hints
 * Purpose:     Active line range hint from upstream processors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "hint.h"
#include <cstdint>

namespace orc {

/**
 * Active Line Range Hint
 * 
 * Provides active line range information from upstream processors like ld-decode.
 * These define the vertical region of the video field/frame that contains
 * visible video content (excluding blanking intervals, VBI, etc.).
 * 
 * This is a HINT because it comes from external metadata (ld-decode's determination),
 * not from orc-core's own analysis of the video signal.
 * 
 * Conforms to HintTraits interface with:
 * - source: HintSource indicating origin of this hint
 * - confidence_pct: 0-100 confidence level
 */
struct ActiveLineHint {
    // Frame-based active line ranges (primary source from metadata)
    int32_t first_active_frame_line = -1;  // First active line in frame
    int32_t last_active_frame_line = -1;   // Last active line in frame
    
    // Field-based active line ranges (calculated from frame-based values)
    int32_t first_active_field_line = -1;  // First active line in field
    int32_t last_active_field_line = -1;   // Last active line in field
    
    /**
     * @brief Source of this hint (common interface)
     */
    HintSource source = HintSource::METADATA;
    
    /**
     * @brief Confidence in this hint (0-100, common interface)
     * 
     * Use HintTraits constants for consistent confidence levels:
     * - METADATA_CONFIDENCE (100): From ld-decode metadata
     * - ANALYSIS_CONFIDENCE (75): Derived from signal analysis  
     * - INHERITED_CONFIDENCE (90): Inherited from source
     * - USER_CONFIDENCE (100): User override
     */
    int confidence_pct = HintTraits::METADATA_CONFIDENCE;
    
    bool is_valid() const {
        return first_active_frame_line >= 0 && last_active_frame_line >= 0;
    }
};

} // namespace orc
