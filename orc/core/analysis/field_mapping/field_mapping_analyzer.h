/*
 * File:        field_mapping_analyzer.h
 * Module:      orc-core/analysis
 * Purpose:     Field mapping analyzer (disc mapper implementation)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "../../include/video_field_representation.h"
#include "../../include/field_id.h"
#include "../../observers/biphase_observer.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace orc {

/**
 * @brief Result of disc mapping analysis
 */
struct FieldMappingDecision {
    std::string mapping_spec;  ///< Field map specification (e.g., "0-10,PAD_5,20-30")
    bool success = false;      ///< True if mapping was successful
    std::string rationale;     ///< Human-readable explanation of decisions
    std::vector<std::string> warnings;  ///< Warnings about potential issues
    bool is_cav = false;       ///< True if CAV disc, false if CLV
    bool is_pal = false;       ///< True if PAL format, false if NTSC
    
    // Statistics for reporting
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
 * Analyzes a VideoFieldRepresentation and its observations to detect
 * and correct field ordering issues caused by laserdisc player problems:
 * - Skips and jumps
 * - Repeated frames
 * - Invalid field sequences
 * - Missing frames (gaps)
 * 
 * Generates a field mapping specification that can be used to configure
 * a FieldMapStage to apply the corrections.
 * 
 * This is the core analysis engine used by DiscMapperAnalysisTool.
 */
class FieldMappingAnalyzer {
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
    
    FieldMappingAnalyzer() = default;
    ~FieldMappingAnalyzer() = default;
    
    /**
     * @brief Analyze source and generate field mapping decision
     * 
     * This function:
     * 1. Runs required observers on all fields
     * 2. Analyzes VBI sequences and quality metrics
     * 3. Detects duplicates, gaps, and errors
     * 4. Generates mapping specification
     * 
     * @param source VideoFieldRepresentation to analyze
     * @param options Configuration options
     * @return FieldMappingDecision with mapping spec and diagnostics
     */
    FieldMappingDecision analyze(
        const VideoFieldRepresentation& source,
        const Options& options = Options{});
    
private:
    // Internal frame information structure
    struct FrameInfo {
        FieldID first_field;
        FieldID second_field;
        int32_t vbi_frame_number = -1;
        int32_t seq_frame_number = -1;  // Sequential frame number in input
        double quality_score = 0.0;
        bool is_pulldown = false;
        bool is_lead_in_out = false;
        bool marked_for_deletion = false;
        bool is_padded = false;
        int first_field_phase = -1;
        int second_field_phase = -1;
    };
    
    // Analysis steps (ported from legacy ld-discmap)
    void remove_lead_in_out(std::vector<FrameInfo>& frames);
    void remove_invalid_frames_by_phase(std::vector<FrameInfo>& frames, VideoFormat format);
    void correct_vbi_using_sequence_analysis(std::vector<FrameInfo>& frames);
    void remove_duplicate_frames(std::vector<FrameInfo>& frames);
    void number_pulldown_frames(std::vector<FrameInfo>& frames);
    bool verify_frame_numbers(const std::vector<FrameInfo>& frames);
    void delete_unmappable_frames(std::vector<FrameInfo>& frames);
    void reorder_frames(std::vector<FrameInfo>& frames);
    void pad_gaps(std::vector<FrameInfo>& frames);
    void renumber_for_pulldown(std::vector<FrameInfo>& frames);
    
    // Helper functions
    std::string generate_mapping_spec(const std::vector<FrameInfo>& frames);
    std::string generate_rationale(const FieldMappingDecision::Stats& stats, bool is_cav, bool is_pal);
    int32_t convert_clv_timecode_to_frame(const CLVTimecode& clv_tc, bool is_pal);
    
    // Current analysis state
    Options current_options_;
    FieldMappingDecision::Stats stats_;
};

} // namespace orc
