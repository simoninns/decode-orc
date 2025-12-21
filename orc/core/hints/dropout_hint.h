/*
 * File:        dropout_hint.h
 * Module:      orc-core/hints
 * Purpose:     Dropout hint from upstream processors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "hint.h"
#include <cstdint>
#include <vector>

namespace orc {

/**
 * @brief Represents a dropout region hint from upstream processor
 * 
 * This represents dropout information that was detected by an upstream
 * processor (like ld-decode) and stored in metadata.
 * 
 * This is a HINT, not an observation - it comes from external sources,
 * not from orc-core's own analysis of the video signal.
 * 
 * Note: This is currently aliased by dropout_decision.h's DropoutRegion
 * for backward compatibility. Eventually dropout_decision.h should use
 * this type directly.
 * 
 * Conforms to HintTraits interface with:
 * - source: HintSource indicating origin of this hint
 * - confidence_pct: 0-100 confidence level
 */
struct DropoutHint {
    uint32_t line = 0;
    uint32_t start_sample = 0;
    uint32_t end_sample = 0;
    
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
     * - CORROBORATED_CONFIDENCE (100): Multiple sources agree
     * - USER_CONFIDENCE (100): User override
     */
    int confidence_pct = 0;
};

} // namespace orc
