/*
 * File:        field_mapping_lookup.h
 * Module:      orc-core/analysis
 * Purpose:     Frame/timecode to field ID lookup utilities
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "../../include/video_field_representation.h"
#include "../../include/field_id.h"
#include "../../include/vbi_types.h"
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace orc {

/**
 * @brief Parsed timecode in CLV format
 */
struct ParsedTimecode {
    int hours;
    int minutes;
    int seconds;
    int picture_number;  // Picture number within second (0-based)
    
    bool is_valid() const {
        return hours >= 0 && minutes >= 0 && minutes < 60 &&
               seconds >= 0 && seconds < 60 &&
               picture_number >= 0;
    }
    
    std::string to_string() const;
};

/**
 * @brief Result of frame/timecode lookup
 */
struct FieldLookupResult {
    bool success = false;
    std::string error_message;
    
    // For single frame/timecode queries
    std::optional<FieldIDRange> field_range;  // The field ID range for the requested frame(s)
    std::optional<int32_t> picture_number;    // CAV picture number if available
    std::optional<ParsedTimecode> timecode;   // CLV timecode if available
    
    // For range queries
    FieldID start_field_id;
    FieldID end_field_id;  // Exclusive
    
    // Metadata
    bool is_cav = false;
    bool is_pal = false;
    std::vector<std::string> warnings;
};

/**
 * @brief Frame/timecode to field ID lookup utility
 * 
 * This class analyzes a VideoFieldRepresentation and builds a mapping
 * from frame numbers and timecodes to field IDs, allowing queries like:
 * - "What field IDs correspond to frames 1000-2000?"
 * - "What field IDs correspond to timecode 0:10:10.28-0:20:10.03?"
 * - "What is the timecode/frame number for field ID 5000?"
 */
class FieldMappingLookup {
public:
    /**
     * @brief Construct a lookup utility from a video source
     * 
     * @param source VideoFieldRepresentation to analyze
     * @throws std::runtime_error if source cannot be analyzed
     */
    explicit FieldMappingLookup(const VideoFieldRepresentation& source);
    
    ~FieldMappingLookup() = default;
    
    /**
     * @brief Parse a timecode string
     * 
     * Supported formats:
     * - H:MM:SS.FF (hours:minutes:seconds.frames)
     * - H:M:S.F (flexible formatting)
     * 
     * @param timecode_str Timecode string to parse
     * @return Parsed timecode or empty optional if invalid
     */
    static std::optional<ParsedTimecode> parse_timecode(const std::string& timecode_str);
    
    /**
     * @brief Get field IDs for a single frame number
     * 
     * @param frame_number Frame number (1-based for CAV, 0-based for internal)
     * @param is_one_based If true, treat frame_number as 1-based (typical for CAV)
     * @return Lookup result with field ID range
     */
    FieldLookupResult get_fields_for_frame(int32_t frame_number, bool is_one_based = false) const;
    
    /**
     * @brief Get field IDs for a frame range
     * 
     * @param start_frame Start frame number (inclusive)
     * @param end_frame End frame number (inclusive)
     * @param is_one_based If true, treat frame numbers as 1-based
     * @return Lookup result with field ID range
     */
    FieldLookupResult get_fields_for_frame_range(int32_t start_frame, int32_t end_frame, 
                                                  bool is_one_based = false) const;
    
    /**
     * @brief Get field IDs for a timecode
     * 
     * @param timecode CLV timecode
     * @return Lookup result with field ID range
     */
    FieldLookupResult get_fields_for_timecode(const ParsedTimecode& timecode) const;
    
    /**
     * @brief Get field IDs for a timecode range
     * 
     * @param start_tc Start timecode (inclusive)
     * @param end_tc End timecode (inclusive)
     * @return Lookup result with field ID range
     */
    FieldLookupResult get_fields_for_timecode_range(const ParsedTimecode& start_tc,
                                                     const ParsedTimecode& end_tc) const;
    
    /**
     * @brief Get frame number and/or timecode for a field ID
     * 
     * @param field_id Field ID to query
     * @return Lookup result with frame number and/or timecode
     */
    FieldLookupResult get_info_for_field(FieldID field_id) const;
    
    /**
     * @brief Check if source is CAV (frame-numbered)
     */
    bool is_cav() const { return is_cav_; }
    
    /**
     * @brief Check if source is CLV (timecode-based)
     */
    bool is_clv() const { return !is_cav_; }
    
    /**
     * @brief Check if source is PAL format
     */
    bool is_pal() const { return is_pal_; }
    
    /**
     * @brief Get total number of frames
     */
    size_t get_frame_count() const { return frame_map_.size(); }
    
    /**
     * @brief Get field ID range covered by this lookup
     */
    FieldIDRange get_field_range() const { return field_range_; }
    
    /**
     * @brief Find field IDs for a timecode range by sequential scan (optimized)
     * 
     * This method scans fields sequentially from the beginning until it finds
     * both the start and end timecodes, then stops. Much more efficient than
     * building a complete mapping for the entire source.
     * 
     * @param source Video source to scan
     * @param start_tc Start timecode
     * @param end_tc End timecode
     * @return Lookup result with field ID range
     */
    static FieldLookupResult find_timecode_range_sequential(
        const VideoFieldRepresentation& source,
        const ParsedTimecode& start_tc,
        const ParsedTimecode& end_tc);
    
    /**
     * @brief Find field IDs for a picture number range by sequential scan (optimized)
     * 
     * Similar to find_timecode_range_sequential but for CAV picture numbers.
     * 
     * @param source Video source to scan
     * @param start_picture Start picture number
     * @param end_picture End picture number
     * @return Lookup result with field ID range
     */
    static FieldLookupResult find_picture_range_sequential(
        const VideoFieldRepresentation& source,
        int32_t start_picture,
        int32_t end_picture);

private:
    struct FrameInfo {
        FieldID first_field;
        FieldID second_field;
        std::optional<int32_t> picture_number;  // CAV frame number
        std::optional<CLVTimecode> clv_timecode;  // CLV timecode
        int sequential_frame_number;  // 0-based sequential position
    };
    
    // Build the frame mapping from VBI data
    void build_mapping(const VideoFieldRepresentation& source);
    
    // Convert timecode to sequential frame number
    int32_t timecode_to_frame_number(const ParsedTimecode& tc) const;
    
    // Convert sequential frame number to timecode
    std::optional<ParsedTimecode> frame_number_to_timecode(int32_t frame_num) const;
    
    bool is_cav_ = false;
    bool is_pal_ = false;
    FieldIDRange field_range_;
    
    // Frame-to-field mapping (sorted by sequential frame number)
    std::vector<FrameInfo> frame_map_;
    
    // Quick lookup indices
    std::map<int32_t, size_t> picture_number_index_;  // CAV picture number -> frame_map_ index
    std::map<FieldID, size_t> field_id_index_;  // FieldID -> frame_map_ index
};

} // namespace orc
