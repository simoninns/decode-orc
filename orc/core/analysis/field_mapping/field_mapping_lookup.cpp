/*
 * File:        field_mapping_lookup.cpp
 * Module:      orc-core/analysis
 * Purpose:     Frame/timecode to field ID lookup utilities implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "field_mapping_lookup.h"
#include "../../include/logging.h"
#include "../../observers/biphase_observer.h"
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <stdexcept>

namespace orc {

// ParsedTimecode methods
std::string ParsedTimecode::to_string() const {
    std::ostringstream oss;
    oss << hours << ":"
        << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << seconds << "."
        << std::setfill('0') << std::setw(2) << picture_number;
    return oss.str();
}

// FieldMappingLookup implementation
FieldMappingLookup::FieldMappingLookup(const VideoFieldRepresentation& source) {
    build_mapping(source);
}

std::optional<ParsedTimecode> FieldMappingLookup::parse_timecode(const std::string& timecode_str) {
    // Match patterns like: H:MM:SS.FF or H:M:S.F
    std::regex tc_regex(R"((\d+):(\d+):(\d+)\.(\d+))");
    std::smatch match;
    
    if (!std::regex_match(timecode_str, match, tc_regex)) {
        return std::nullopt;
    }
    
    ParsedTimecode tc;
    tc.hours = std::stoi(match[1]);
    tc.minutes = std::stoi(match[2]);
    tc.seconds = std::stoi(match[3]);
    tc.picture_number = std::stoi(match[4]);
    
    if (!tc.is_valid()) {
        return std::nullopt;
    }
    
    return tc;
}

void FieldMappingLookup::build_mapping(const VideoFieldRepresentation& source) {
    field_range_ = source.field_range();
    
    if (!field_range_.is_valid() || field_range_.size() < 2) {
        throw std::runtime_error("Invalid or empty field range in source");
    }
    
    // Determine format (PAL/NTSC) from first available field
    auto first_descriptor = source.get_descriptor(field_range_.start);
    if (!first_descriptor) {
        throw std::runtime_error("Cannot get descriptor for first field");
    }
    
    is_pal_ = (first_descriptor->format == VideoFormat::PAL);
    
    ORC_LOG_DEBUG("Building field mapping lookup: {} fields, format {}",
                  field_range_.size(), is_pal_ ? "PAL" : "NTSC");
    
    // Build frame mapping by pairing fields
    int sequential_frame = 0;
    bool has_picture_numbers = false;
    bool has_timecodes = false;
    
    for (FieldID fid = field_range_.start; fid < field_range_.end; ) {
        // Get descriptor for first field
        auto desc1 = source.get_descriptor(fid);
        if (!desc1) {
            ++fid;
            continue;
        }
        
        // Try to get second field
        FieldID second_fid = fid + 1;
        if (second_fid >= field_range_.end) {
            // Orphan field at end
            break;
        }
        
        auto desc2 = source.get_descriptor(second_fid);
        if (!desc2) {
            ++fid;
            continue;
        }
        
        // Create frame info
        FrameInfo frame;
        frame.first_field = fid;
        frame.second_field = second_fid;
        frame.sequential_frame_number = sequential_frame;
        
        // Check for VBI data (picture number or timecode)
        if (desc1->frame_number.has_value()) {
            frame.picture_number = desc1->frame_number;
            has_picture_numbers = true;
        } else if (desc2->frame_number.has_value()) {
            frame.picture_number = desc2->frame_number;
            has_picture_numbers = true;
        }
        
        if (desc1->timecode.has_value()) {
            // Decode timecode from uint32_t (packed format from VBI)
            uint32_t tc_val = desc1->timecode.value();
            CLVTimecode clv;
            clv.hours = (tc_val >> 24) & 0xFF;
            clv.minutes = (tc_val >> 16) & 0xFF;
            clv.seconds = (tc_val >> 8) & 0xFF;
            clv.picture_number = tc_val & 0xFF;
            frame.clv_timecode = clv;
            has_timecodes = true;
        } else if (desc2->timecode.has_value()) {
            uint32_t tc_val = desc2->timecode.value();
            CLVTimecode clv;
            clv.hours = (tc_val >> 24) & 0xFF;
            clv.minutes = (tc_val >> 16) & 0xFF;
            clv.seconds = (tc_val >> 8) & 0xFF;
            clv.picture_number = tc_val & 0xFF;
            frame.clv_timecode = clv;
            has_timecodes = true;
        }
        
        frame_map_.push_back(frame);
        
        // Build indices
        if (frame.picture_number.has_value()) {
            picture_number_index_[frame.picture_number.value()] = frame_map_.size() - 1;
        }
        field_id_index_[fid] = frame_map_.size() - 1;
        field_id_index_[second_fid] = frame_map_.size() - 1;
        
        sequential_frame++;
        fid = second_fid + 1;
    }
    
    // Determine if CAV or CLV
    is_cav_ = has_picture_numbers && !has_timecodes;
    
    ORC_LOG_INFO("Built field mapping: {} frames, {} ({})",
                 frame_map_.size(),
                 is_cav_ ? "CAV" : "CLV",
                 is_pal_ ? "PAL" : "NTSC");
    
    if (frame_map_.empty()) {
        throw std::runtime_error("No valid frames found in source");
    }
}

FieldLookupResult FieldMappingLookup::get_fields_for_frame(int32_t frame_number, bool is_one_based) const {
    FieldLookupResult result;
    result.is_cav = is_cav_;
    result.is_pal = is_pal_;
    
    if (is_cav_) {
        // For CAV, use picture number lookup
        int32_t lookup_frame = is_one_based ? frame_number : (frame_number + 1);
        
        auto it = picture_number_index_.find(lookup_frame);
        if (it == picture_number_index_.end()) {
            result.success = false;
            result.error_message = "Frame number " + std::to_string(frame_number) + " not found in source";
            return result;
        }
        
        const auto& frame = frame_map_[it->second];
        result.success = true;
        result.field_range = FieldIDRange(frame.first_field, frame.second_field + 1);
        result.start_field_id = frame.first_field;
        result.end_field_id = frame.second_field + 1;
        result.picture_number = frame.picture_number;
        
    } else {
        // For CLV, use sequential frame number
        int32_t seq_frame = is_one_based ? (frame_number - 1) : frame_number;
        
        if (seq_frame < 0 || static_cast<size_t>(seq_frame) >= frame_map_.size()) {
            result.success = false;
            result.error_message = "Frame number " + std::to_string(frame_number) + " out of range";
            return result;
        }
        
        const auto& frame = frame_map_[seq_frame];
        result.success = true;
        result.field_range = FieldIDRange(frame.first_field, frame.second_field + 1);
        result.start_field_id = frame.first_field;
        result.end_field_id = frame.second_field + 1;
        
        if (frame.clv_timecode.has_value()) {
            const auto& clv = frame.clv_timecode.value();
            result.timecode = ParsedTimecode{clv.hours, clv.minutes, clv.seconds, clv.picture_number};
        }
    }
    
    return result;
}

FieldLookupResult FieldMappingLookup::get_fields_for_frame_range(int32_t start_frame, int32_t end_frame,
                                                                  bool is_one_based) const {
    FieldLookupResult result;
    result.is_cav = is_cav_;
    result.is_pal = is_pal_;
    
    if (start_frame > end_frame) {
        result.success = false;
        result.error_message = "Invalid range: start_frame > end_frame";
        return result;
    }
    
    // Get start and end field ranges
    auto start_result = get_fields_for_frame(start_frame, is_one_based);
    auto end_result = get_fields_for_frame(end_frame, is_one_based);
    
    if (!start_result.success) {
        return start_result;
    }
    if (!end_result.success) {
        return end_result;
    }
    
    result.success = true;
    result.start_field_id = start_result.start_field_id;
    result.end_field_id = end_result.end_field_id;
    result.field_range = FieldIDRange(result.start_field_id, result.end_field_id);
    
    return result;
}

int32_t FieldMappingLookup::timecode_to_frame_number(const ParsedTimecode& tc) const {
    // Calculate total frames from timecode
    int frames_per_second = is_pal_ ? 25 : 30;  // Note: NTSC is actually 29.97, but VBI uses 30
    
    int total_seconds = tc.hours * 3600 + tc.minutes * 60 + tc.seconds;
    int total_frames = total_seconds * frames_per_second + tc.picture_number;
    
    return total_frames;
}

std::optional<ParsedTimecode> FieldMappingLookup::frame_number_to_timecode(int32_t frame_num) const {
    if (frame_num < 0 || static_cast<size_t>(frame_num) >= frame_map_.size()) {
        return std::nullopt;
    }
    
    const auto& frame = frame_map_[frame_num];
    if (!frame.clv_timecode.has_value()) {
        return std::nullopt;
    }
    
    const auto& clv = frame.clv_timecode.value();
    return ParsedTimecode{clv.hours, clv.minutes, clv.seconds, clv.picture_number};
}

FieldLookupResult FieldMappingLookup::get_fields_for_timecode(const ParsedTimecode& timecode) const {
    FieldLookupResult result;
    result.is_cav = is_cav_;
    result.is_pal = is_pal_;
    
    if (!is_clv()) {
        result.success = false;
        result.error_message = "Timecode lookup only available for CLV sources";
        return result;
    }
    
    // Find matching timecode in frame map
    for (const auto& frame : frame_map_) {
        if (frame.clv_timecode.has_value()) {
            const auto& clv = frame.clv_timecode.value();
            if (clv.hours == timecode.hours &&
                clv.minutes == timecode.minutes &&
                clv.seconds == timecode.seconds &&
                clv.picture_number == timecode.picture_number) {
                
                result.success = true;
                result.field_range = FieldIDRange(frame.first_field, frame.second_field + 1);
                result.start_field_id = frame.first_field;
                result.end_field_id = frame.second_field + 1;
                result.timecode = timecode;
                return result;
            }
        }
    }
    
    result.success = false;
    result.error_message = "Timecode " + timecode.to_string() + " not found in source";
    return result;
}

FieldLookupResult FieldMappingLookup::get_fields_for_timecode_range(const ParsedTimecode& start_tc,
                                                                     const ParsedTimecode& end_tc) const {
    FieldLookupResult result;
    result.is_cav = is_cav_;
    result.is_pal = is_pal_;
    
    auto start_result = get_fields_for_timecode(start_tc);
    auto end_result = get_fields_for_timecode(end_tc);
    
    if (!start_result.success) {
        return start_result;
    }
    if (!end_result.success) {
        return end_result;
    }
    
    result.success = true;
    result.start_field_id = start_result.start_field_id;
    result.end_field_id = end_result.end_field_id;
    result.field_range = FieldIDRange(result.start_field_id, result.end_field_id);
    
    return result;
}

FieldLookupResult FieldMappingLookup::get_info_for_field(FieldID field_id) const {
    FieldLookupResult result;
    result.is_cav = is_cav_;
    result.is_pal = is_pal_;
    
    auto it = field_id_index_.find(field_id);
    if (it == field_id_index_.end()) {
        result.success = false;
        result.error_message = "Field ID " + field_id.to_string() + " not found";
        return result;
    }
    
    const auto& frame = frame_map_[it->second];
    
    result.success = true;
    result.start_field_id = frame.first_field;
    result.end_field_id = frame.second_field + 1;
    result.field_range = FieldIDRange(frame.first_field, frame.second_field + 1);
    
    if (frame.picture_number.has_value()) {
        result.picture_number = frame.picture_number;
    }
    
    if (frame.clv_timecode.has_value()) {
        const auto& clv = frame.clv_timecode.value();
        result.timecode = ParsedTimecode{clv.hours, clv.minutes, clv.seconds, clv.picture_number};
    }
    
    return result;
}

} // namespace orc
