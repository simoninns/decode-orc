/*
 * File:        hint.h
 * Module:      orc-core/hints
 * Purpose:     Common base interface for all hint types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <cstdint>

namespace orc {

/**
 * @brief Common hint source types
 * 
 * All hints in orc-core share a common set of source types to maintain
 * consistency and allow generic hint handling.
 */
enum class HintSource {
    METADATA,        ///< From metadata database (e.g., ld-decode's TBC metadata)
    USER_OVERRIDE,   ///< User manually specified
    INHERITED,       ///< Inherited from source field in processing chain
    SAMPLE_ANALYSIS, ///< Derived from orc-core's own signal analysis
    CORROBORATED     ///< Multiple sources agree (metadata + analysis)
};

/**
 * @brief Base interface for all hint types
 * 
 * This provides common fields that all hints should include:
 * - source: Where the hint came from
 * - confidence_pct: How confident we are in the hint (0-100)
 * 
 * All hint structs should include these fields to maintain consistency
 * and allow generic hint processing.
 * 
 * Design Note:
 * We use a convention-based approach rather than inheritance to keep
 * hints as simple POD-like structs. Each hint type should include:
 * 
 *   HintSource source = HintSource::METADATA;
 *   int confidence_pct = 0;  // 0-100
 * 
 * This allows hints to be easily serialized, copied, and used in
 * constexpr contexts while maintaining a consistent interface.
 */
struct HintTraits {
    /**
     * @brief Default confidence for metadata-sourced hints
     * 
     * Hints from metadata (like ld-decode's determinations) are
     * considered authoritative and get 100% confidence.
     */
    static constexpr int METADATA_CONFIDENCE = 100;
    
    /**
     * @brief Default confidence for analysis-derived hints
     * 
     * Hints derived from orc-core's own analysis get lower confidence
     * since they may not have access to all timing information.
     */
    static constexpr int ANALYSIS_CONFIDENCE = 75;
    
    /**
     * @brief Default confidence for inherited hints
     * 
     * Hints inherited from source fields maintain their original
     * confidence unless modified by processing.
     */
    static constexpr int INHERITED_CONFIDENCE = 90;
    
    /**
     * @brief Default confidence for user overrides
     * 
     * User-specified hints are considered authoritative.
     */
    static constexpr int USER_CONFIDENCE = 100;
    
    /**
     * @brief Default confidence for corroborated hints
     * 
     * When multiple sources agree, confidence is maximized.
     */
    static constexpr int CORROBORATED_CONFIDENCE = 100;
};

} // namespace orc
