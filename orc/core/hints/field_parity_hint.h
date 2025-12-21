/*
 * File:        field_parity_hint.h
 * Module:      orc-core/hints
 * Purpose:     Field parity hint from upstream processors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "hint.h"

namespace orc {

/**
 * @brief Field parity hint from upstream processor (e.g., ld-decode)
 * 
 * This represents field parity information that was determined by
 * an upstream processor (like ld-decode) and stored in metadata.
 * 
 * This is a HINT, not an observation - it comes from external sources,
 * not from orc-core's own analysis of the video signal.
 * 
 * Stages should prefer hints when available, and only use observers
 * when processing raw/uncategorized video data.
 * 
 * Conforms to HintTraits interface with:
 * - source: HintSource indicating origin of this hint
 * - confidence_pct: 0-100 confidence level
 */
struct FieldParityHint {
    /**
     * @brief Whether this is the first field (odd/top) in an interlaced pair
     * 
     * Interpretation depends on video system:
     * - NTSC: first field = Field 1 (starts on whole line)
     * - PAL:  first field = Field 1 (starts on half line)
     */
    bool is_first_field;
    
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
    int confidence_pct = 0;
};

} // namespace orc
