/*
 * File:        field_mapping_lookup.cpp
 * Module:      orc-core/analysis
 * Purpose:     Frame/timecode to field ID lookup utilities implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "field_mapping_lookup.h"
#include "../../include/logging.h"
#include "../../observers/biphase_observer.h"
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <cmath>

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
        
        // Check for VBI data (picture number or timecode) from observations
        // Get observations for both fields
        auto obs1 = source.get_observations(fid);
        auto obs2 = source.get_observations(second_fid);
        
        // Helper to find BiphaseObservation
        auto find_biphase = [](const std::vector<std::shared_ptr<Observation>>& observations) 
            -> std::shared_ptr<BiphaseObservation> {
            for (const auto& obs : observations) {
                if (obs && obs->observation_type() == "Biphase") {
                    return std::dynamic_pointer_cast<BiphaseObservation>(obs);
                }
            }
            return nullptr;
        };
        
        auto biphase1 = find_biphase(obs1);
        auto biphase2 = find_biphase(obs2);
        
        // Get picture number from observations
        if (biphase1 && biphase1->picture_number.has_value()) {
            frame.picture_number = biphase1->picture_number;
            has_picture_numbers = true;
        } else if (biphase2 && biphase2->picture_number.has_value()) {
            frame.picture_number = biphase2->picture_number;
            has_picture_numbers = true;
        }
        
        // Get timecode from observations
        if (biphase1 && biphase1->clv_timecode.has_value()) {
            frame.clv_timecode = biphase1->clv_timecode;
            has_timecodes = true;
        } else if (biphase2 && biphase2->clv_timecode.has_value()) {
            frame.clv_timecode = biphase2->clv_timecode;
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
    
    ORC_LOG_DEBUG("Built field mapping: {} frames, {} ({})",
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
    
    // Convert timecode to frame number for comparison
    int32_t target_frame = timecode_to_frame_number(timecode);
    
    // Find the closest frame with timecode data
    size_t best_match_idx = 0;
    int32_t best_distance = std::numeric_limits<int32_t>::max();
    bool found_any = false;
    
    for (size_t i = 0; i < frame_map_.size(); ++i) {
        const auto& frame = frame_map_[i];
        if (frame.clv_timecode.has_value()) {
            const auto& clv = frame.clv_timecode.value();
            ParsedTimecode frame_tc{clv.hours, clv.minutes, clv.seconds, clv.picture_number};
            int32_t frame_num = timecode_to_frame_number(frame_tc);
            int32_t distance = std::abs(frame_num - target_frame);
            
            if (distance < best_distance) {
                best_distance = distance;
                best_match_idx = i;
                found_any = true;
            }
            
            // If we found an exact match, use it immediately
            if (distance == 0) {
                break;
            }
        }
    }
    
    if (!found_any) {
        result.success = false;
        result.error_message = "No timecode data found in source";
        return result;
    }
    
    const auto& frame = frame_map_[best_match_idx];
    result.success = true;
    result.field_range = FieldIDRange(frame.first_field, frame.second_field + 1);
    result.start_field_id = frame.first_field;
    result.end_field_id = frame.second_field + 1;
    result.timecode = timecode;
    
    // Add warning if not an exact match
    if (best_distance > 0) {
        const auto& clv = frame.clv_timecode.value();
        ParsedTimecode actual_tc{clv.hours, clv.minutes, clv.seconds, clv.picture_number};
        result.warnings.push_back("Exact timecode not found, using closest match: " + actual_tc.to_string());
    }
    
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

FieldLookupResult FieldMappingLookup::find_timecode_range_sequential(
    const VideoFieldRepresentation& source,
    const ParsedTimecode& start_tc,
    const ParsedTimecode& end_tc) {
    
    FieldLookupResult result;
    
    auto field_range = source.field_range();
    if (!field_range.is_valid()) {
        result.success = false;
        result.error_message = "Invalid field range";
        return result;
    }
    
    // Get video format
    auto first_descriptor = source.get_descriptor(field_range.start);
    if (!first_descriptor) {
        result.success = false;
        result.error_message = "Cannot get descriptor";
        return result;
    }
    result.is_pal = (first_descriptor->format == VideoFormat::PAL);
    result.is_cav = false;
    
    // Convert timecodes to frame numbers for comparison
    int frames_per_second = result.is_pal ? 25 : 30;
    
    auto tc_to_frame = [frames_per_second](const ParsedTimecode& tc) -> int32_t {
        int total_seconds = tc.hours * 3600 + tc.minutes * 60 + tc.seconds;
        return total_seconds * frames_per_second + tc.picture_number;
    };
    
    int32_t target_start = tc_to_frame(start_tc);
    int32_t target_end = tc_to_frame(end_tc);
    
    ORC_LOG_DEBUG("Sequential timecode scan: looking for frames {} to {}", target_start, target_end);
    
    // Scan fields sequentially
    FieldID start_field_id;
    FieldID end_field_id;
    bool found_start = false;
    bool found_end = false;
    
    for (FieldID fid = field_range.start; fid < field_range.end && !found_end; ++fid) {
        auto observations = source.get_observations(fid);
        
        for (const auto& obs : observations) {
            if (obs && obs->observation_type() == "Biphase") {
                auto biphase = std::dynamic_pointer_cast<BiphaseObservation>(obs);
                if (biphase && biphase->clv_timecode.has_value()) {
                    const auto& clv = biphase->clv_timecode.value();
                    ParsedTimecode field_tc{clv.hours, clv.minutes, clv.seconds, clv.picture_number};
                    int32_t field_frame = tc_to_frame(field_tc);
                    
                    if (!found_start && field_frame >= target_start) {
                        start_field_id = fid;
                        found_start = true;
                        ORC_LOG_DEBUG("Found start at field {} (tc: {})", fid.value(), field_tc.to_string());
                    }
                    
                    if (found_start && field_frame >= target_end) {
                        end_field_id = fid + 1;  // Exclusive
                        found_end = true;
                        ORC_LOG_DEBUG("Found end at field {} (tc: {})", fid.value(), field_tc.to_string());
                        break;
                    }
                }
            }
        }
    }
    
    if (!found_start) {
        result.success = false;
        result.error_message = "Start timecode " + start_tc.to_string() + " not found in source";
        return result;
    }
    
    if (!found_end) {
        // Reached end of source - use last field
        end_field_id = field_range.end;
        result.warnings.push_back("End timecode not found, using end of source");
    }
    
    result.success = true;
    result.start_field_id = start_field_id;
    result.end_field_id = end_field_id;
    result.field_range = FieldIDRange(start_field_id, end_field_id);
    
    return result;
}

FieldLookupResult FieldMappingLookup::find_picture_range_sequential(
    const VideoFieldRepresentation& source,
    int32_t start_picture,
    int32_t end_picture) {
    
    FieldLookupResult result;
    
    auto field_range = source.field_range();
    if (!field_range.is_valid()) {
        result.success = false;
        result.error_message = "Invalid field range";
        return result;
    }
    
    // Get video format
    auto first_descriptor = source.get_descriptor(field_range.start);
    if (!first_descriptor) {
        result.success = false;
        result.error_message = "Cannot get descriptor";
        return result;
    }
    result.is_pal = (first_descriptor->format == VideoFormat::PAL);
    result.is_cav = true;
    
    ORC_LOG_DEBUG("Sequential picture scan: looking for pictures {} to {}", start_picture, end_picture);
    
    // Scan fields sequentially
    FieldID start_field_id;
    FieldID end_field_id;
    bool found_start = false;
    bool found_end = false;
    
    for (FieldID fid = field_range.start; fid < field_range.end && !found_end; ++fid) {
        auto observations = source.get_observations(fid);
        
        for (const auto& obs : observations) {
            if (obs && obs->observation_type() == "Biphase") {
                auto biphase = std::dynamic_pointer_cast<BiphaseObservation>(obs);
                if (biphase && biphase->picture_number.has_value()) {
                    int32_t pic_num = biphase->picture_number.value();
                    
                    if (!found_start && pic_num >= start_picture) {
                        start_field_id = fid;
                        found_start = true;
                        ORC_LOG_DEBUG("Found start at field {} (picture: {})", fid.value(), pic_num);
                    }
                    
                    if (found_start && pic_num >= end_picture) {
                        end_field_id = fid + 1;  // Exclusive
                        found_end = true;
                        ORC_LOG_DEBUG("Found end at field {} (picture: {})", fid.value(), pic_num);
                        break;
                    }
                }
            }
        }
    }
    
    if (!found_start) {
        result.success = false;
        result.error_message = "Start picture number " + std::to_string(start_picture) + " not found in source";
        return result;
    }
    
    if (!found_end) {
        // Reached end of source - use last field
        end_field_id = field_range.end;
        result.warnings.push_back("End picture number not found, using end of source");
    }
    
    result.success = true;
    result.start_field_id = start_field_id;
    result.end_field_id = end_field_id;
    result.field_range = FieldIDRange(start_field_id, end_field_id);
    
    return result;
}

} // namespace orc
