/*
 * File:        pal_phase_hint.h
 * Module:      orc-core/hints
 * Purpose:     PAL phase hint from upstream processors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "hint.h"

namespace orc {

/**
 * PAL Phase Hint
 * 
 * Provides PAL phase ID (1-8) information from upstream processors like ld-decode.
 * PAL uses an 8-field color sequence due to the PAL color encoding system.
 * 
 * This is a HINT because it comes from external metadata (ld-decode's determination),
 * not from orc-core's own analysis of the video signal.
 * 
 * Conforms to HintTraits interface with:
 * - source: HintSource indicating origin of this hint
 * - confidence_pct: 0-100 confidence level
 */
struct PALPhaseHint {
    int32_t field_phase_id;  // PAL phase (1-8), or -1 if unable to determine
    
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
