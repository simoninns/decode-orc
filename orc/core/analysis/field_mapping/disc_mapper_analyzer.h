/*
 * File:        disc_mapper_analyzer.h
 * Module:      orc-core/analysis
 * Purpose:     Field mapping analyzer (disc mapper implementation)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "../../include/video_field_representation.h"
#include "../../include/field_id.h"
#include <string>
#include <vector>

namespace orc {

// Forward declaration
class AnalysisProgress;

/**
 * @brief Result of disc mapping analysis
 */
struct FieldMappingDecision {
    std::string mapping_spec;
    bool success = false;
    std::string rationale;
    std::vector<std::string> warnings;
    bool is_cav = false;
    bool is_pal = false;

    struct Stats {
        size_t total_fields = 0;
        size_t removed_lead_in_out = 0;
        size_t removed_invalid_phase = 0;
        size_t removed_duplicates = 0;
        size_t removed_unmappable = 0;
        size_t corrected_vbi_errors = 0;
        size_t pulldown_frames = 0;
        size_t padding_frames = 0;
        size_t gaps_padded = 0;
    } stats;
};

/**
 * @brief Field mapping analyzer
 * 
 * Stubbed analyzer. Currently returns a failure result explaining that
 * disc mapping is disabled.
 */
class DiscMapperAnalyzer {
public:
    /**
     * @brief Configuration options for disc mapping analysis
     */
    struct Options {
        bool delete_unmappable_frames;  ///< Remove frames that can't be mapped
        bool strict_pulldown_checking;   ///< Enforce strict pulldown patterns
        bool reverse_field_order;       ///< Reverse first/second field order
        bool pad_gaps;                   ///< Insert padding for missing frames
        
        // Default constructor with sensible defaults
        Options() 
            : delete_unmappable_frames(false)
            , strict_pulldown_checking(true)
            , reverse_field_order(false)
            , pad_gaps(true) {}
    };
    
    DiscMapperAnalyzer() = default;
    ~DiscMapperAnalyzer() = default;
    
    /**
     * @brief Stubbed analyze implementation that reports disc mapping is disabled.
     */
    FieldMappingDecision analyze(
        const VideoFieldRepresentation& source,
        const Options& options = Options{},
        class AnalysisProgress* progress = nullptr);
};

} // namespace orc
