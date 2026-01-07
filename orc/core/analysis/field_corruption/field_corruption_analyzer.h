/*
 * File:        field_corruption_analyzer.h
 * Module:      orc-core/analysis
 * Purpose:     Field corruption pattern generator for testing disc mapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <random>

namespace orc {

/**
 * @brief Field corruption pattern generator
 * 
 * Generates field mapping range specifications that simulate laserdisc
 * player corruption patterns (skips, repeats, gaps). Used for testing
 * the disc mapper and field correction algorithms.
 * 
 * This analyzer creates range specifications compatible with FieldMapStage,
 * allowing corruption to be applied within the DAG chain rather than
 * requiring separate corrupted TBC files.
 */
class FieldCorruptionAnalyzer {
public:
    /**
     * @brief Predefined corruption patterns
     */
    enum class Pattern {
        SIMPLE_SKIP,      // Skip 5 fields every 100 fields
        SIMPLE_REPEAT,    // Repeat 3 fields every 50 fields
        SKIP_WITH_GAP,    // Skip 10 fields, insert 5 gap markers every 200 fields
        HEAVY_SKIP,       // Skip 15 fields every 100 fields (severe damage)
        HEAVY_REPEAT,     // Repeat 5 fields every 30 fields (severe sticking)
        MIXED_LIGHT,      // Light mix of skips and repeats
        MIXED_HEAVY       // Heavy mix of skips, repeats, and gap markers
    };
    
    /**
     * @brief Corruption pattern configuration
     */
    struct PatternConfig {
        std::string name;
        std::string description;
        uint32_t skip_fields;         // Number of fields to skip
        uint32_t repeat_fields;       // Number of times to repeat a field
        uint32_t gap_marker_count;    // Number of gap markers to insert
        double corruption_rate;       // Probability of corruption event (0.0-1.0)
    };
    
    /**
     * @brief Corruption event record
     */
    struct CorruptionEvent {
        enum Type { SKIP, REPEAT, GAP };
        Type type;
        uint64_t start_field;
        uint64_t end_field;
        uint32_t count;
        
        std::string to_string() const;
    };
    
    /**
     * @brief Analysis result containing corruption specification
     */
    struct Result {
        std::string mapping_spec;      ///< Field mapping range specification
        bool success = false;          ///< True if generation succeeded
        std::string rationale;         ///< Description of pattern applied
        std::vector<CorruptionEvent> events;  ///< List of corruption events
        
        struct Stats {
            uint64_t normal_fields = 0;
            uint64_t repeated_fields = 0;
            uint64_t skipped_fields = 0;
            uint64_t gap_markers = 0;
            uint64_t total_output_fields = 0;
        } stats;
    };
    
    /**
     * @brief Construct analyzer with field count and pattern
     * @param total_fields Total number of input fields
     * @param pattern Corruption pattern to apply
     * @param seed Random seed (0 = random device)
     */
    FieldCorruptionAnalyzer(uint64_t total_fields, Pattern pattern, uint32_t seed = 0);
    
    /**
     * @brief Construct analyzer with custom pattern config
     */
    FieldCorruptionAnalyzer(uint64_t total_fields, const PatternConfig& config, uint32_t seed = 0);
    
    /**
     * @brief Generate corruption pattern
     * @return Result containing mapping specification and statistics
     */
    Result analyze();
    
    /**
     * @brief Get predefined pattern configuration
     */
    static PatternConfig get_pattern_config(Pattern pattern);
    
    /**
     * @brief Get all available pattern configs with descriptions
     */
    static std::vector<PatternConfig> get_all_patterns();

private:
    uint64_t total_fields_;
    PatternConfig config_;
    std::mt19937 rng_;
    
    // Temporary storage for events/stats during generation
    std::vector<CorruptionEvent> events_;
    Result::Stats stats_;
    
    /**
     * @brief Build field mapping with corruption applied
     * @return Vector where each element is an input field ID
     *         (0xFFFFFFFF = gap marker)
     */
    std::vector<uint64_t> build_mapping();
    
    /**
     * @brief Convert field mapping to range specification string
     */
    std::string mapping_to_ranges(const std::vector<uint64_t>& mapping);
};

} // namespace orc
