/*
 * File:        field_mapping_analyzer.cpp
 * Module:      orc-core/analysis
 * Purpose:     Field mapping analyzer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "field_mapping_analyzer.h"
#include "../../observers/observation_history.h"
#include "../../observers/biphase_observer.h"
#include "../../observers/pal_phase_observer.h"
#include "../../include/disc_quality_observer.h"
#include "../../include/pulldown_observer.h"
#include "../../include/lead_in_out_observer.h"
#include "../../observers/field_parity_observer.h"
#include "../../include/logging.h"
#include <algorithm>
#include <sstream>
#include <set>

namespace orc {

FieldMappingDecision FieldMappingAnalyzer::analyze(
    const VideoFieldRepresentation& source,
    const Options& options) {
    
    current_options_ = options;
    stats_ = FieldMappingDecision::Stats{};
    
    FieldMappingDecision decision;
    
    ORC_LOG_INFO("Disc mapping analysis starting...");
    
    // Step 1: Run observers on all fields to collect data
    ORC_LOG_INFO("Running observers on all fields...");
    
    ObservationHistory history;
    std::vector<std::shared_ptr<Observer>> observers;
    observers.push_back(std::make_shared<BiphaseObserver>());
    observers.push_back(std::make_shared<PALPhaseObserver>());
    observers.push_back(std::make_shared<FieldParityObserver>());
    observers.push_back(std::make_shared<DiscQualityObserver>());
    observers.push_back(std::make_shared<PulldownObserver>());
    observers.push_back(std::make_shared<LeadInOutObserver>());
    
    auto field_range = source.field_range();
    stats_.total_fields = field_range.size();
    
    // Run observers and populate history
    for (FieldID field_id = field_range.start; field_id <= field_range.end; ++field_id) {
        std::vector<std::shared_ptr<Observation>> all_observations;
        for (const auto& observer : observers) {
            auto observations = observer->process_field(source, field_id, history);
            all_observations.insert(all_observations.end(), observations.begin(), observations.end());
        }
        history.add_observations(field_id, all_observations);
    }
    
    ORC_LOG_INFO("Observers complete, processing {} fields", stats_.total_fields);
    ORC_LOG_DEBUG("Field range: {} to {}", field_range.start.value(), field_range.end.value());
    
    // Step 2: Build frame map from fields
    // Combine pairs of fields into frames
    std::vector<FrameInfo> frames;
    
    ORC_LOG_INFO("Building frame map from field pairs...");
    
    // Determine video format from first field descriptor
    auto first_descriptor = source.get_descriptor(field_range.start);
    VideoFormat format = first_descriptor ? first_descriptor->format : VideoFormat::NTSC;
    bool is_pal = (format == VideoFormat::PAL);
    bool is_cav = false;  // Determine from VBI
    
    ORC_LOG_DEBUG("Video format: {}", is_pal ? "PAL" : "NTSC");
    
    // Group fields into frames (2 fields per frame)
    for (size_t i = 0; i + 1 < field_range.size(); i += 2) {
        FieldID first_id(field_range.start.value() + i);
        FieldID second_id(field_range.start.value() + i + 1);
        
        FrameInfo frame;
        frame.first_field = first_id;
        frame.second_field = second_id;
        frame.seq_frame_number = static_cast<int32_t>(frames.size());
        
        // Get VBI data
        auto vbi_first_ptr = history.get_observation(first_id, "Biphase");
        auto vbi_second_ptr = history.get_observation(second_id, "Biphase");
        auto vbi_first = std::dynamic_pointer_cast<BiphaseObservation>(vbi_first_ptr);
        auto vbi_second = std::dynamic_pointer_cast<BiphaseObservation>(vbi_second_ptr);
        
        // Check for CAV picture number
        if (vbi_first && vbi_first->picture_number.has_value()) {
            frame.vbi_frame_number = vbi_first->picture_number.value();
            is_cav = true;
        } else if (vbi_second && vbi_second->picture_number.has_value()) {
            frame.vbi_frame_number = vbi_second->picture_number.value();
            is_cav = true;
        }
        // Check for CLV timecode and convert to frame number
        else if (vbi_first && vbi_first->clv_timecode.has_value()) {
            is_cav = false;
            frame.vbi_frame_number = convert_clv_timecode_to_frame(vbi_first->clv_timecode.value(), is_pal);
        } else if (vbi_second && vbi_second->clv_timecode.has_value()) {
            is_cav = false;
            frame.vbi_frame_number = convert_clv_timecode_to_frame(vbi_second->clv_timecode.value(), is_pal);
        }
        
        // Get phase data
        if (is_pal) {
            // Get PAL phase observations (8-field sequence for PAL)
            auto phase_first_ptr = history.get_observation(first_id, "PALPhase");
            auto phase_second_ptr = history.get_observation(second_id, "PALPhase");
            auto phase_first = std::dynamic_pointer_cast<PALPhaseObservation>(phase_first_ptr);
            auto phase_second = std::dynamic_pointer_cast<PALPhaseObservation>(phase_second_ptr);
            
            frame.first_field_phase = phase_first ? phase_first->field_phase_id : -1;
            frame.second_field_phase = phase_second ? phase_second->field_phase_id : -1;
        } else {
            // NTSC phase is simpler (just field sequence)
            frame.first_field_phase = static_cast<int>(i % 4) + 1;
            frame.second_field_phase = static_cast<int>((i + 1) % 4) + 1;
        }
        
        // Get quality
        auto quality_first_ptr = history.get_observation(first_id, "DiscQuality");
        auto quality_second_ptr = history.get_observation(second_id, "DiscQuality");
        auto quality_first = std::dynamic_pointer_cast<DiscQualityObservation>(quality_first_ptr);
        auto quality_second = std::dynamic_pointer_cast<DiscQualityObservation>(quality_second_ptr);
        if (quality_first && quality_second) {
            frame.quality_score = (quality_first->quality_score + quality_second->quality_score) / 2.0;
        }
        
        // Get pulldown status
        auto pulldown_first_ptr = history.get_observation(first_id, "Pulldown");
        auto pulldown_second_ptr = history.get_observation(second_id, "Pulldown");
        auto pulldown_first = std::dynamic_pointer_cast<PulldownObservation>(pulldown_first_ptr);
        auto pulldown_second = std::dynamic_pointer_cast<PulldownObservation>(pulldown_second_ptr);
        frame.is_pulldown = (pulldown_first && pulldown_first->is_pulldown) ||
                           (pulldown_second && pulldown_second->is_pulldown);
        
        // Get lead-in/out status
        auto lead_first_ptr = history.get_observation(first_id, "LeadInOut");
        auto lead_second_ptr = history.get_observation(second_id, "LeadInOut");
        auto lead_first = std::dynamic_pointer_cast<LeadInOutObservation>(lead_first_ptr);
        auto lead_second = std::dynamic_pointer_cast<LeadInOutObservation>(lead_second_ptr);
        frame.is_lead_in_out = (lead_first && lead_first->is_lead_in_out) ||
                               (lead_second && lead_second->is_lead_in_out);
        
        frames.push_back(frame);
    }
    
    ORC_LOG_INFO("Built frame map with {} frames (format={} disc_type={})",
                 frames.size(), is_pal ? "PAL" : "NTSC", is_cav ? "CAV" : "CLV");
    
    // Log first few frames for debugging
    size_t debug_count = std::min(size_t(5), frames.size());
    ORC_LOG_DEBUG("First {} frames:", debug_count);
    for (size_t i = 0; i < debug_count; ++i) {
        ORC_LOG_DEBUG("  Frame {}: Fields {}-{}, VBI={}, Quality={:.2f}, Pulldown={}, LeadInOut={}",
                     i, frames[i].first_field.value(), frames[i].second_field.value(),
                     frames[i].vbi_frame_number, frames[i].quality_score,
                     frames[i].is_pulldown, frames[i].is_lead_in_out);
    }
    
    // Step 3: Apply analysis and corrections
    ORC_LOG_INFO("Applying disc mapping corrections...");
    remove_lead_in_out(frames);
    ORC_LOG_DEBUG("After lead-in/out removal: {} frames remaining", frames.size());
    
    remove_invalid_frames_by_phase(frames, format);
    ORC_LOG_DEBUG("After phase validation: {} frames remaining", frames.size());
    
    correct_vbi_using_sequence_analysis(frames);
    ORC_LOG_DEBUG("After VBI correction: {} frames", frames.size());
    
    remove_duplicate_frames(frames);
    ORC_LOG_DEBUG("After duplicate removal: {} frames remaining", frames.size());
    
    if (!is_pal && is_cav) {
        number_pulldown_frames(frames);
    }
    
    // Verify all frames have numbers
    if (!verify_frame_numbers(frames)) {
        if (options.delete_unmappable_frames) {
            ORC_LOG_WARN("Some frames unmappable, deleting as requested");
            delete_unmappable_frames(frames);
        } else {
            decision.success = false;
            decision.rationale = "Disc mapping failed: unmappable frames detected. "
                                "Try with delete_unmappable_frames option.";
            decision.warnings.push_back("Unmappable frames present");
            return decision;
        }
    }
    
    reorder_frames(frames);
    ORC_LOG_DEBUG("After reordering: {} frames", frames.size());
    
    if (options.pad_gaps) {
        pad_gaps(frames);
        ORC_LOG_DEBUG("After gap padding: {} frames", frames.size());
    }
    
    if (!is_pal && is_cav && stats_.pulldown_frames > 0) {
        renumber_for_pulldown(frames);
        ORC_LOG_DEBUG("After pulldown renumbering: {} frames", frames.size());
    }
    
    // Step 4: Generate mapping specification
    ORC_LOG_INFO("Generating field mapping specification...");
    decision.mapping_spec = generate_mapping_spec(frames);
    decision.stats = stats_;
    decision.is_cav = is_cav;
    decision.is_pal = is_pal;
    decision.success = true;
    decision.rationale = generate_rationale(stats_, is_cav, is_pal);
    
    ORC_LOG_INFO("Disc mapping analysis complete");
    ORC_LOG_INFO("  Final frame count: {}", frames.size());
    ORC_LOG_INFO("  Mapping spec length: {} chars", decision.mapping_spec.length());
    if (decision.mapping_spec.length() <= 200) {
        ORC_LOG_INFO("  Mapping spec: {}", decision.mapping_spec);
    } else {
        ORC_LOG_INFO("  Mapping spec (first 200 chars): {}...", decision.mapping_spec.substr(0, 200));
    }
    ORC_LOG_INFO("  Stats: removed_lead={} invalid_phase={} duplicates={} gaps={} padding={}",
                 stats_.removed_lead_in_out, stats_.removed_invalid_phase,
                 stats_.removed_duplicates, stats_.gaps_padded, stats_.padding_frames);
    
    return decision;
}

void FieldMappingAnalyzer::remove_lead_in_out(std::vector<FrameInfo>& frames) {
    ORC_LOG_INFO("Removing lead-in/out frames...");
    
    size_t removed = 0;
    for (auto& frame : frames) {
        if (frame.is_lead_in_out) {
            frame.marked_for_deletion = true;
            removed++;
        }
        
        // Also remove CAV frame 0 (illegal)
        if (frame.vbi_frame_number == 0 && !frame.is_lead_in_out) {
            ORC_LOG_WARN("Frame with illegal CAV frame number 0 found, marking for deletion");
            frame.marked_for_deletion = true;
            removed++;
        }
    }
    
    // Remove marked frames
    frames.erase(
        std::remove_if(frames.begin(), frames.end(),
                      [](const FrameInfo& f) { return f.marked_for_deletion; }),
        frames.end());
    
    stats_.removed_lead_in_out = removed;
    ORC_LOG_INFO("Removed {} lead-in/out frames", removed);
}

void FieldMappingAnalyzer::remove_invalid_frames_by_phase(
    std::vector<FrameInfo>& frames,
    VideoFormat format) {
    
    ORC_LOG_INFO("Removing frames with invalid phase sequences...");
    
    size_t removed = 0;
    for (auto& frame : frames) {
        // Check that first and second field phases are sequential
        int expected_next = frame.first_field_phase + 1;
        
        if (format == VideoFormat::PAL) {
            if (expected_next == 9) expected_next = 1;  // PAL wraps at 8
        } else {
            if (expected_next == 5) expected_next = 1;  // NTSC wraps at 4
        }
        
        if (frame.second_field_phase != expected_next && 
            frame.first_field_phase != -1 && 
            frame.second_field_phase != -1) {
            frame.marked_for_deletion = true;
            removed++;
        }
    }
    
    // Remove marked frames
    frames.erase(
        std::remove_if(frames.begin(), frames.end(),
                      [](const FrameInfo& f) { return f.marked_for_deletion; }),
        frames.end());
    
    stats_.removed_invalid_phase = removed;
    ORC_LOG_INFO("Removed {} frames with invalid phase sequences", removed);
}

void FieldMappingAnalyzer::correct_vbi_using_sequence_analysis(std::vector<FrameInfo>& frames) {
    ORC_LOG_INFO("Correcting VBI frame numbers using sequence analysis...");
    
    const int scan_distance = 10;
    size_t corrections = 0;
    
    for (size_t i = 0; i < frames.size() && i + scan_distance < frames.size(); ++i) {
        if (frames[i].is_pulldown || frames[i].vbi_frame_number == -1) {
            continue;
        }
        
        int32_t start_vbi = frames[i].vbi_frame_number;
        int32_t expected_increment = 1;
        
        std::vector<bool> vbi_good(scan_distance, false);
        bool sequence_good = true;
        
        for (int j = 0; j < scan_distance; ++j) {
            size_t idx = i + j + 1;
            if (idx >= frames.size()) break;
            
            if (!frames[idx].is_pulldown) {
                sequence_good = (frames[idx].vbi_frame_number == start_vbi + expected_increment);
                vbi_good[j] = sequence_good;
                expected_increment++;
            } else {
                vbi_good[j] = sequence_good;
            }
        }
        
        // Count good frames
        int good_count = std::count(vbi_good.begin(), vbi_good.end(), true);
        
        // If we have enough good frames before and after errors, correct them
        if (good_count < scan_distance && good_count >= scan_distance / 2) {
            expected_increment = 1;
            for (int j = 0; j < scan_distance; ++j) {
                size_t idx = i + j + 1;
                if (idx >= frames.size()) break;
                
                if (!vbi_good[j] && !frames[idx].is_pulldown) {
                    frames[idx].vbi_frame_number = start_vbi + expected_increment;
                    corrections++;
                }
                if (!frames[idx].is_pulldown) {
                    expected_increment++;
                }
            }
        }
    }
    
    stats_.corrected_vbi_errors = corrections;
    ORC_LOG_INFO("Corrected {} VBI frame numbers using sequence analysis", corrections);
}

void FieldMappingAnalyzer::remove_duplicate_frames(std::vector<FrameInfo>& frames) {
    ORC_LOG_INFO("Removing duplicate frames...");
    
    // Find all VBI numbers that appear more than once
    std::map<int32_t, std::vector<size_t>> vbi_to_frames;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (!frames[i].is_pulldown && frames[i].vbi_frame_number != -1) {
            vbi_to_frames[frames[i].vbi_frame_number].push_back(i);
        }
    }
    
    // Count duplicates
    size_t duplicate_vbi_count = 0;
    for (const auto& [vbi_num, indices] : vbi_to_frames) {
        if (indices.size() > 1) {
            duplicate_vbi_count++;
        }
    }
    ORC_LOG_DEBUG("Found {} VBI frame numbers with duplicates", duplicate_vbi_count);
    
    size_t removed = 0;
    
    // For each duplicated VBI number, keep the best quality
    for (const auto& [vbi_num, indices] : vbi_to_frames) {
        if (indices.size() > 1) {
            // Find best quality
            size_t best_idx = indices[0];
            double best_quality = frames[best_idx].quality_score;
            
            for (size_t idx : indices) {
                if (frames[idx].quality_score > best_quality) {
                    best_quality = frames[idx].quality_score;
                    best_idx = idx;
                }
            }
            
            ORC_LOG_DEBUG("VBI {}: {} duplicates found, keeping frame {} (quality={:.2f})",
                         vbi_num, indices.size(), frames[best_idx].seq_frame_number, best_quality);
            
            // Mark all others for deletion
            for (size_t idx : indices) {
                if (idx != best_idx) {
                    frames[idx].marked_for_deletion = true;
                    removed++;
                }
            }
        }
    }
    
    // Remove marked frames
    frames.erase(
        std::remove_if(frames.begin(), frames.end(),
                      [](const FrameInfo& f) { return f.marked_for_deletion; }),
        frames.end());
    
    stats_.removed_duplicates = removed;
    ORC_LOG_INFO("Removed {} duplicate frames", removed);
}

void FieldMappingAnalyzer::number_pulldown_frames(std::vector<FrameInfo>& frames) {
    ORC_LOG_INFO("Numbering pulldown frames...");
    
    size_t pulldown_count = 0;
    
    // Give pulldown frames the same number as previous frame
    for (size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].is_pulldown) {
            frames[i].vbi_frame_number = frames[i-1].vbi_frame_number;
            pulldown_count++;
        }
    }
    
    // Handle first frame if it's pulldown (edge case)
    if (!frames.empty() && frames[0].is_pulldown && frames.size() > 1) {
        frames[0].vbi_frame_number = frames[1].vbi_frame_number - 1;
        ORC_LOG_WARN("First frame is pulldown - assigned number {}", frames[0].vbi_frame_number);
    }
    
    stats_.pulldown_frames = pulldown_count;
    ORC_LOG_INFO("Numbered {} pulldown frames", pulldown_count);
}

bool FieldMappingAnalyzer::verify_frame_numbers(const std::vector<FrameInfo>& frames) {
    ORC_LOG_INFO("Verifying all frames have valid frame numbers...");
    
    for (const auto& frame : frames) {
        if (frame.vbi_frame_number < 0) {
            ORC_LOG_WARN("Frame {} has invalid VBI number {}", 
                           frame.seq_frame_number, frame.vbi_frame_number);
            return false;
        }
    }
    
    ORC_LOG_INFO("Verification successful - all frames have valid numbers");
    return true;
}

void FieldMappingAnalyzer::delete_unmappable_frames(std::vector<FrameInfo>& frames) {
    ORC_LOG_INFO("Deleting unmappable frames...");
    
    size_t removed = 0;
    for (auto& frame : frames) {
        if (frame.vbi_frame_number < 0 && !frame.is_pulldown) {
            frame.marked_for_deletion = true;
            removed++;
        }
    }
    
    frames.erase(
        std::remove_if(frames.begin(), frames.end(),
                      [](const FrameInfo& f) { return f.marked_for_deletion; }),
        frames.end());
    
    stats_.removed_unmappable = removed;
    ORC_LOG_INFO("Deleted {} unmappable frames", removed);
}

void FieldMappingAnalyzer::reorder_frames(std::vector<FrameInfo>& frames) {
    ORC_LOG_INFO("Sorting frames by VBI number...");
    
    std::sort(frames.begin(), frames.end(),
        [](const FrameInfo& a, const FrameInfo& b) {
            if (a.vbi_frame_number != b.vbi_frame_number) {
                return a.vbi_frame_number < b.vbi_frame_number;
            }
            // If same VBI number, pulldown comes after normal
            return !a.is_pulldown && b.is_pulldown;
        });
    
    ORC_LOG_INFO("Sorting complete");
}

void FieldMappingAnalyzer::pad_gaps(std::vector<FrameInfo>& frames) {
    ORC_LOG_INFO("Padding gaps in frame sequence...");
    
    std::vector<FrameInfo> padded_frames;
    size_t gaps = 0;
    size_t total_padding = 0;
    
    for (size_t i = 0; i < frames.size(); ++i) {
        padded_frames.push_back(frames[i]);
        
        if (i + 1 < frames.size()) {
            int32_t current_vbi = frames[i].vbi_frame_number;
            int32_t next_vbi = frames[i+1].vbi_frame_number;
            
            // Skip if current is pulldown or gap is due to pulldown
            if (frames[i].is_pulldown || frames[i+1].is_pulldown) {
                continue;
            }
            
            int32_t gap_size = next_vbi - current_vbi - 1;
            
            if (gap_size > 0 && gap_size < 1000) {  // Sanity check
                // Insert padding frames
                for (int32_t j = 0; j < gap_size; ++j) {
                    FrameInfo pad_frame;
                    pad_frame.is_padded = true;
                    pad_frame.vbi_frame_number = current_vbi + j + 1;
                    padded_frames.push_back(pad_frame);
                }
                gaps++;
                total_padding += gap_size;
            } else if (gap_size >= 1000) {
                ORC_LOG_WARN("Large gap detected ({} frames), skipping padding", gap_size);
            }
        }
    }
    
    frames = std::move(padded_frames);
    stats_.gaps_padded = gaps;
    stats_.padding_frames = total_padding;
    
    ORC_LOG_INFO("Padded {} gaps with {} total padding frames", gaps, total_padding);
}

void FieldMappingAnalyzer::renumber_for_pulldown(std::vector<FrameInfo>& frames) {
    ORC_LOG_INFO("Renumbering all frames to include pulldown frames...");
    
    int32_t new_vbi = frames.empty() ? 0 : frames[0].vbi_frame_number;
    for (auto& frame : frames) {
        frame.vbi_frame_number = new_vbi++;
    }
    
    ORC_LOG_INFO("Renumbering complete");
}

std::string FieldMappingAnalyzer::generate_mapping_spec(const std::vector<FrameInfo>& frames) {
    // Generate field map specification string
    // Format: "field_range,PAD_count,field_range,..."
    // Field ranges use FIELD IDs (from the source), not frame indices
    
    std::ostringstream spec;
    bool first = true;
    
    uint64_t current_range_start_field = 0;
    bool in_real_range = false;
    size_t pad_count = 0;
    
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].is_padded) {
            // End current real range if any
            if (in_real_range && i > 0) {
                if (!first) spec << ",";
                // Use the second field ID of the previous frame as the end of range
                uint64_t range_end_field = frames[i-1].second_field.value();
                spec << current_range_start_field << "-" << range_end_field;
                first = false;
                in_real_range = false;
            }
            pad_count++;
        } else {
            // Output pending padding if any
            if (pad_count > 0) {
                if (!first) spec << ",";
                spec << "PAD_" << pad_count;
                first = false;
                pad_count = 0;
            }
            
            // Start or continue real range
            if (!in_real_range) {
                // Use the first field ID of this frame as the start of range
                current_range_start_field = frames[i].first_field.value();
                in_real_range = true;
            }
        }
    }
    
    // Output final range or padding
    if (in_real_range) {
        if (!first) spec << ",";
        // Use the second field ID of the last non-padded frame
        uint64_t range_end_field = frames.back().second_field.value();
        if (frames.back().is_padded) {
            // Find last non-padded frame
            for (int i = static_cast<int>(frames.size()) - 1; i >= 0; --i) {
                if (!frames[i].is_padded) {
                    range_end_field = frames[i].second_field.value();
                    break;
                }
            }
        }
        spec << current_range_start_field << "-" << range_end_field;
    } else if (pad_count > 0) {
        if (!first) spec << ",";
        spec << "PAD_" << pad_count;
    }
    
    return spec.str();
}

std::string FieldMappingAnalyzer::generate_rationale(
    const FieldMappingDecision::Stats& stats,
    bool is_cav,
    bool is_pal) {
    
    std::ostringstream rationale;
    
    rationale << "Disc mapping analysis complete.\n";
    rationale << "Disc type: " << (is_pal ? "PAL" : "NTSC") << " " << (is_cav ? "CAV" : "CLV") << "\n";
    rationale << "Total frames processed: " << stats.total_fields / 2 << "\n\n";
    
    rationale << "Operations performed:\n";
    
    if (stats.removed_lead_in_out > 0) {
        rationale << "  - Removed " << stats.removed_lead_in_out << " lead-in/lead-out frames\n";
    }
    
    if (stats.removed_invalid_phase > 0) {
        rationale << "  - Removed " << stats.removed_invalid_phase 
                  << " frames with invalid phase sequences\n";
    }
    
    if (stats.corrected_vbi_errors > 0) {
        rationale << "  - Corrected " << stats.corrected_vbi_errors 
                  << " VBI frame number errors using sequence analysis\n";
    }
    
    if (stats.removed_duplicates > 0) {
        rationale << "  - Removed " << stats.removed_duplicates 
                  << " duplicate frames (keeping best quality)\n";
    }
    
    if (stats.pulldown_frames > 0) {
        rationale << "  - Numbered " << stats.pulldown_frames << " pulldown frames\n";
    }
    
    if (stats.gaps_padded > 0) {
        rationale << "  - Padded " << stats.gaps_padded << " gaps with " 
                  << stats.padding_frames << " black frames\n";
    }
    
    if (stats.removed_unmappable > 0) {
        rationale << "  - Removed " << stats.removed_unmappable << " unmappable frames\n";
    }
    
    return rationale.str();
}

int32_t FieldMappingAnalyzer::convert_clv_timecode_to_frame(const CLVTimecode& clv_tc, bool is_pal) {
    // Convert CLV timecode to frame number
    // Based on legacy LdDecodeMetaData::convertClvTimecodeToFrameNumber
    
    // Check for invalid timecode
    if (clv_tc.hours == -1 && clv_tc.minutes == -1 && 
        clv_tc.seconds == -1 && clv_tc.picture_number == -1) {
        return -1;
    }
    
    int32_t frame_number = 0;
    int32_t fps = is_pal ? 25 : 30;
    
    if (clv_tc.hours != -1) {
        frame_number += clv_tc.hours * 3600 * fps;
    }
    
    if (clv_tc.minutes != -1) {
        frame_number += clv_tc.minutes * 60 * fps;
    }
    
    if (clv_tc.seconds != -1) {
        frame_number += clv_tc.seconds * fps;
    }
    
    if (clv_tc.picture_number != -1) {
        frame_number += clv_tc.picture_number;
    }
    
    return frame_number;
}

} // namespace orc
