/*
 * File:        field_mapping_analyzer.cpp
 * Module:      orc-core/analysis
 * Purpose:     Field mapping analyzer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "field_mapping_analyzer.h"
#include "../analysis_progress.h"
#include "../../observers/observation_history.h"
#include "../../observers/biphase_observer.h"
#include "../../observers/pulldown_observer.h"
#include "../../observers/lead_in_out_observer.h"
#include "../../include/logging.h"
#include <algorithm>
#include <sstream>
#include <set>

namespace orc {

FieldMappingDecision FieldMappingAnalyzer::analyze(
    const VideoFieldRepresentation& source,
    const Options& options,
    AnalysisProgress* progress) {
    
    current_options_ = options;
    stats_ = FieldMappingDecision::Stats{};
    
    FieldMappingDecision decision;
    
    ORC_LOG_INFO("Disc mapping analysis starting...");
    
    // Step 1: Collect observations from source
    ORC_LOG_INFO("Collecting observations from source...");
    
    ObservationHistory history;
    auto field_range = source.field_range();
    stats_.total_fields = field_range.size();
    
    // Collect observations from source (provided by source stage)
    size_t field_count = 0;
    for (FieldID field_id = field_range.start; field_id < field_range.end; ++field_id) {
        auto observations = source.get_observations(field_id);
        history.add_observations(field_id, observations);
        
        // Update progress every 100 fields
        if (progress && (++field_count % 100 == 0 || field_id.value() == field_range.end.value() - 1)) {
            int percentage = 20 + (field_count * 50 / stats_.total_fields);
            progress->setProgress(percentage);
            progress->setSubStatus("Collecting observations " + std::to_string(field_count) + "/" + std::to_string(stats_.total_fields));
            
            if (progress->isCancelled()) {
                decision.success = false;
                decision.rationale = "Analysis cancelled by user";
                return decision;
            }
        }
    }
    
    ORC_LOG_DEBUG("Collected observations for {} fields (field range: {} to {})", 
                 stats_.total_fields, field_range.start.value(), field_range.end.value() - 1);
    ORC_LOG_DEBUG("Field IDs: {} to {}", field_range.start.value(), field_range.end.value());
    
    // Step 2: Build frame map from fields
    // Combine pairs of fields into frames
    std::vector<FrameInfo> frames;
    
    ORC_LOG_DEBUG("Building frame map from field pairs...");
    
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
        
        // Get phase data from hints (works for both PAL and NTSC)
        // Phase comes from TBC metadata field_phase_id:
        // - PAL: 8-field sequence (1-8)
        // - NTSC: 4-field sequence (1-4)
        auto phase_first = source.get_field_phase_hint(first_id);
        auto phase_second = source.get_field_phase_hint(second_id);
        
        frame.first_field_phase = phase_first.has_value() ? phase_first->field_phase_id : -1;
        frame.second_field_phase = phase_second.has_value() ? phase_second->field_phase_id : -1;
        
        // Quality score removed - was calculated from dropouts which we already have
        frame.quality_score = 1.0;  // Default to perfect quality
        
        // Get pulldown status
        auto pulldown_first_ptr = history.get_observation(first_id, "Pulldown");
        auto pulldown_second_ptr = history.get_observation(second_id, "Pulldown");
        auto pulldown_first = std::dynamic_pointer_cast<PulldownObservation>(pulldown_first_ptr);
        auto pulldown_second = std::dynamic_pointer_cast<PulldownObservation>(pulldown_second_ptr);
        frame.is_pulldown = (pulldown_first && pulldown_first->is_pulldown) ||
                           (pulldown_second && pulldown_second->is_pulldown);
        
        // Get lead-in/out status from BiphaseObservation
        bool is_lead_in_out = false;
        if (vbi_first && (vbi_first->lead_in || vbi_first->lead_out)) {
            is_lead_in_out = true;
        }
        if (vbi_second && (vbi_second->lead_in || vbi_second->lead_out)) {
            is_lead_in_out = true;
        }
        // Also check for illegal CAV frame 0
        if ((vbi_first && vbi_first->picture_number.has_value() && vbi_first->picture_number.value() == 0) ||
            (vbi_second && vbi_second->picture_number.has_value() && vbi_second->picture_number.value() == 0)) {
            is_lead_in_out = true;
        }
        frame.is_lead_in_out = is_lead_in_out;
        
        frames.push_back(frame);
    }
    
    ORC_LOG_DEBUG("Built frame map with {} frames (format={} disc_type={})",
                 frames.size(), is_pal ? "PAL" : "NTSC", is_cav ? "CAV" : "CLV");
    
    if (progress) {
        progress->setProgress(72);
        progress->setStatus("Building frame map...");
    }
    
    // Log first few frames for debugging
    size_t debug_count = std::min(size_t(5), frames.size());
    ORC_LOG_DEBUG("First {} frames (each frame = 2 fields):", debug_count);
    for (size_t i = 0; i < debug_count; ++i) {
        ORC_LOG_DEBUG("  Frame {}: field IDs {}-{}, VBI frame#={}, Quality={:.2f}, Pulldown={}, LeadInOut={}",
                     i, frames[i].first_field.value(), frames[i].second_field.value(),
                     frames[i].vbi_frame_number, frames[i].quality_score,
                     frames[i].is_pulldown, frames[i].is_lead_in_out);
    }
    
    // Step 3: Apply analysis and corrections
    if (progress) {
        progress->setProgress(75);
        progress->setStatus("Applying corrections...");
    }
    
    ORC_LOG_DEBUG("Applying disc mapping corrections...");
    remove_lead_in_out(frames);
    ORC_LOG_DEBUG("After lead-in/out removal: {} frames remaining", frames.size());
    
    remove_invalid_frames_by_phase(frames, format);
    ORC_LOG_DEBUG("After phase validation: {} frames remaining", frames.size());
    
    correct_vbi_using_sequence_analysis(frames, format);
    ORC_LOG_DEBUG("After VBI correction: {} frames", frames.size());
    
    remove_duplicate_frames(frames);
    ORC_LOG_DEBUG("After duplicate removal: {} frames remaining", frames.size());
    
    if (!is_pal && is_cav) {
        number_pulldown_frames(frames);
    }
    
    // Verify all frames have numbers
    size_t unmappable_count = 0;
    for (const auto& frame : frames) {
        if (frame.vbi_frame_number < 0) {
            unmappable_count++;
        }
    }
    
    if (!verify_frame_numbers(frames)) {
        if (options.delete_unmappable_frames) {
            ORC_LOG_WARN("Some frames unmappable, deleting as requested");
            delete_unmappable_frames(frames);
        } else {
            decision.success = false;
            decision.rationale = fmt::format("Disc mapping failed: {} unmappable frame(s) detected out of {} total frames. "
                                "Try with delete_unmappable_frames option.", 
                                unmappable_count, frames.size());
            decision.warnings.push_back(fmt::format("Unmappable frames present: {} of {} frames", 
                                                   unmappable_count, frames.size()));
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
    if (progress) {
        progress->setProgress(85);
        progress->setStatus("Generating mapping specification...");
    }
    
    ORC_LOG_DEBUG("Generating field mapping specification...");
    ORC_LOG_DEBUG("Before generate_mapping_spec: {} frames remaining", frames.size());
    if (frames.size() > 0) {
        ORC_LOG_DEBUG("  First frame: fields {}-{}", 
                     frames.front().first_field.value(), frames.front().second_field.value());
        ORC_LOG_DEBUG("  Last frame: fields {}-{}", 
                     frames.back().first_field.value(), frames.back().second_field.value());
    }
    decision.mapping_spec = generate_mapping_spec(frames);
    decision.stats = stats_;
    decision.is_cav = is_cav;
    decision.is_pal = is_pal;
    decision.success = true;
    decision.rationale = generate_rationale(stats_, is_cav, is_pal);
    
    ORC_LOG_INFO("Disc mapping analysis complete");
    ORC_LOG_DEBUG("  Input: {} fields ({} field pairs/frames)", stats_.total_fields, stats_.total_fields / 2);
    ORC_LOG_DEBUG("  Output: {} frames ({} fields)", frames.size(), frames.size() * 2);
    ORC_LOG_DEBUG("  Mapping spec length: {} chars", decision.mapping_spec.length());
    if (decision.mapping_spec.length() <= 200) {
        ORC_LOG_DEBUG("  Mapping spec: {}", decision.mapping_spec);
    } else {
        ORC_LOG_DEBUG("  Mapping spec (first 200 chars): {}...", decision.mapping_spec.substr(0, 200));
    }
    ORC_LOG_DEBUG("  Frames removed: lead-in/out={} invalid_phase={} duplicates={} unmappable={}",
                 stats_.removed_lead_in_out, stats_.removed_invalid_phase,
                 stats_.removed_duplicates, stats_.removed_unmappable);
    ORC_LOG_DEBUG("  Frames added: gap_padding={} (filled {} gaps)",
                 stats_.padding_frames, stats_.gaps_padded);
    
    return decision;
}

void FieldMappingAnalyzer::remove_lead_in_out(std::vector<FrameInfo>& frames) {
    ORC_LOG_DEBUG("Removing lead-in/out frames...");
    
    size_t removed = 0;
    for (auto& frame : frames) {
        if (frame.is_lead_in_out) {
            ORC_LOG_DEBUG("Removing lead-in/out frame: seq={}, VBI={}", 
                         frame.seq_frame_number, frame.vbi_frame_number);
            frame.marked_for_deletion = true;
            removed++;
        }
        
        // Also remove CAV frame 0 (illegal)
        if (frame.vbi_frame_number == 0 && !frame.is_lead_in_out) {
            ORC_LOG_WARN("Removing frame with illegal CAV frame number 0: seq={}", 
                        frame.seq_frame_number);
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
    ORC_LOG_DEBUG("Removed {} lead-in/out frames", removed);
}

void FieldMappingAnalyzer::remove_invalid_frames_by_phase(
    std::vector<FrameInfo>& frames,
    VideoFormat format) {
    
    ORC_LOG_DEBUG("Removing frames with invalid phase sequences...");
    
    size_t removed = 0;
    for (auto& frame : frames) {
        // Check that first and second field phases are sequential
        int expected_next = frame.first_field_phase + 1;
        
        if (format == VideoFormat::PAL) {
            if (expected_next == 9) expected_next = 1;  // PAL wraps at 8
        } else {
            if (expected_next == 5) expected_next = 1;  // NTSC wraps at 4
        }
        
        // Log phase info for debugging (first 10 frames)
        if (frame.seq_frame_number < 10 || frame.seq_frame_number == 29 || frame.seq_frame_number == 79 || frame.seq_frame_number == 109) {
            ORC_LOG_DEBUG("Frame {} (VBI# {}): phases {}/{}, expected second={}", 
                         frame.seq_frame_number, frame.vbi_frame_number,
                         frame.first_field_phase, frame.second_field_phase, expected_next);
        }
        
        // Remove frames where field phases are not in sequence
        // This matches ld-discmap behavior - invalid phase means the frame is broken
        if (frame.second_field_phase != expected_next && 
            frame.first_field_phase != -1 && 
            frame.second_field_phase != -1) {
            if (frame.vbi_frame_number != -1) {
                ORC_LOG_DEBUG("Marking frame {} for deletion (VBI Frame# {}): phases not in sequence (expected {}, got {})",
                             frame.seq_frame_number, frame.vbi_frame_number, expected_next, frame.second_field_phase);
            } else {
                ORC_LOG_DEBUG("Marking frame {} for deletion (no VBI): phases not in sequence (expected {}, got {})",
                             frame.seq_frame_number, expected_next, frame.second_field_phase);
            }
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
    ORC_LOG_DEBUG("Removed {} frames with invalid phase sequences", removed);
}

void FieldMappingAnalyzer::correct_vbi_using_sequence_analysis(std::vector<FrameInfo>& frames, VideoFormat format) {
    ORC_LOG_DEBUG("Correcting VBI frame numbers using sequence analysis...");
    
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
        
        // If all frames are good, nothing to correct
        if (good_count == scan_distance) {
            continue;
        }
        
        // Count good frames before first error
        int check1 = 0;
        for (int j = 0; j < scan_distance; ++j) {
            size_t idx = i + j + 1;
            if (idx >= frames.size()) break;
            if (vbi_good[j] && !frames[idx].is_pulldown) {
                check1++;
            } else if (!frames[idx].is_pulldown) {
                break;
            }
        }
        
        // Count good frames after last error (scanning backwards)
        int check2 = 0;
        for (int j = scan_distance - 1; j >= 0; --j) {
            size_t idx = i + j + 1;
            if (idx >= frames.size()) break;
            if (vbi_good[j] && !frames[idx].is_pulldown) {
                check2++;
            } else if (!frames[idx].is_pulldown) {
                break;
            }
        }
        
        // Need at least 2 good frames before and after errors to be confident
        if (check1 >= 2 && check2 >= 2) {
            bool in_error = false;
            expected_increment = 1;
            
            for (int j = 0; j < scan_distance; ++j) {
                size_t idx = i + j + 1;
                if (idx >= frames.size()) break;
                
                if (!vbi_good[j]) {
                    in_error = true;
                    
                    if (!frames[idx].is_pulldown) {
                        // Only correct if:
                        // 1. It's not a repeating frame (different VBI from previous)
                        // 2. The phase is correct (not a real gap/skip)
                        bool is_repeating = (idx > 0 && 
                                           frames[idx].vbi_frame_number == frames[idx-1].vbi_frame_number);
                        
                        bool has_correct_phase = true;
                        if (idx > 0 && frames[idx].first_field_phase != -1 && 
                            frames[idx-1].second_field_phase != -1) {
                            int expected_phase = frames[idx-1].second_field_phase + 1;
                            // PAL wraps at 8, NTSC at 4
                            if (format == VideoFormat::PAL && expected_phase == 9) {
                                expected_phase = 1;
                            } else if (format == VideoFormat::NTSC && expected_phase == 5) {
                                expected_phase = 1;
                            }
                            has_correct_phase = (frames[idx].first_field_phase == expected_phase);
                        }
                        
                        if (!is_repeating && has_correct_phase) {
                            ORC_LOG_DEBUG("Correcting VBI: seq frame {} VBI {} -> {}", 
                                         frames[idx].seq_frame_number,
                                         frames[idx].vbi_frame_number,
                                         start_vbi + expected_increment);
                            frames[idx].vbi_frame_number = start_vbi + expected_increment;
                            corrections++;
                        } else if (is_repeating) {
                            // Check if phases also repeat (true repeating frame)
                            bool phase_repeats = false;
                            if (idx > 0) {
                                phase_repeats = (frames[idx].first_field_phase == frames[idx-1].first_field_phase &&
                                               frames[idx].second_field_phase == frames[idx-1].second_field_phase);
                            }
                            if (phase_repeats) {
                                ORC_LOG_DEBUG("Ignoring sequence break at seq frame {}: frame is repeating (VBI and phase)",
                                            frames[idx].seq_frame_number);
                                // This is a real repeat, stop processing this sequence
                                if (in_error) break;
                            }
                        }
                        expected_increment++;
                    }
                } else {
                    // Good frame
                    if (!frames[idx].is_pulldown) {
                        expected_increment++;
                    }
                    // Stop once we get a good frame after the bad ones
                    if (in_error) break;
                }
            }
        }
    }
    
    stats_.corrected_vbi_errors = corrections;
    ORC_LOG_DEBUG("Corrected {} VBI frame numbers using sequence analysis", corrections);
}

void FieldMappingAnalyzer::remove_duplicate_frames(std::vector<FrameInfo>& frames) {
    ORC_LOG_DEBUG("Removing duplicate frames...");
    
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
    ORC_LOG_DEBUG("Found {} distinct VBI frame numbers that appear multiple times", duplicate_vbi_count);
    
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
            
            ORC_LOG_DEBUG("VBI frame #{}: {} duplicates, keeping seq frame {} (quality={:.2f})",
                         vbi_num, indices.size(), frames[best_idx].seq_frame_number, best_quality);
            
            // Mark all others for deletion
            for (size_t idx : indices) {
                if (idx != best_idx) {
                    ORC_LOG_DEBUG("  Removing duplicate: seq frame {} (quality={:.2f})",
                                 frames[idx].seq_frame_number, frames[idx].quality_score);
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
    ORC_LOG_DEBUG("Removed {} duplicate frames", removed);
}

void FieldMappingAnalyzer::number_pulldown_frames(std::vector<FrameInfo>& frames) {
    ORC_LOG_DEBUG("Numbering pulldown frames...");
    
    size_t pulldown_count = 0;
    
    // Give pulldown frames the same number as previous frame
    for (size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].is_pulldown) {
            ORC_LOG_DEBUG("Numbering pulldown frame: seq={}, assigned VBI={} (from previous frame)",
                         frames[i].seq_frame_number, frames[i-1].vbi_frame_number);
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
    ORC_LOG_DEBUG("Numbered {} pulldown frames", pulldown_count);
}

bool FieldMappingAnalyzer::verify_frame_numbers(const std::vector<FrameInfo>& frames) {
    ORC_LOG_DEBUG("Verifying all frames have valid VBI frame numbers...");
    
    size_t unmappable = 0;
    for (const auto& frame : frames) {
        if (frame.vbi_frame_number < 0) {
            ORC_LOG_WARN("Unmappable frame found: seq={}, VBI={}, pulldown={}", 
                         frame.seq_frame_number, frame.vbi_frame_number, frame.is_pulldown);
            unmappable++;
        }
    }
    
    if (unmappable > 0) {
        ORC_LOG_WARN("Verification failed: {} frames have invalid VBI numbers", unmappable);
        return false;
    }
    
    ORC_LOG_DEBUG("Verification successful - all frames have valid VBI frame numbers");
    return true;
}

void FieldMappingAnalyzer::delete_unmappable_frames(std::vector<FrameInfo>& frames) {
    ORC_LOG_DEBUG("Deleting unmappable frames...");
    
    size_t removed = 0;
    for (auto& frame : frames) {
        if (frame.vbi_frame_number < 0 && !frame.is_pulldown) {
            ORC_LOG_DEBUG("Deleting unmappable frame: seq={}, VBI={}",
                         frame.seq_frame_number, frame.vbi_frame_number);
            frame.marked_for_deletion = true;
            removed++;
        }
    }
    
    frames.erase(
        std::remove_if(frames.begin(), frames.end(),
                      [](const FrameInfo& f) { return f.marked_for_deletion; }),
        frames.end());
    
    stats_.removed_unmappable = removed;
    ORC_LOG_DEBUG("Deleted {} unmappable frames", removed);
}

void FieldMappingAnalyzer::reorder_frames(std::vector<FrameInfo>& frames) {
    ORC_LOG_DEBUG("Sorting frames by VBI number...");
    
    std::sort(frames.begin(), frames.end(),
        [](const FrameInfo& a, const FrameInfo& b) {
            if (a.vbi_frame_number != b.vbi_frame_number) {
                return a.vbi_frame_number < b.vbi_frame_number;
            }
            // If same VBI number, pulldown comes after normal
            return !a.is_pulldown && b.is_pulldown;
        });
    
    ORC_LOG_DEBUG("Sorting complete");
}

void FieldMappingAnalyzer::pad_gaps(std::vector<FrameInfo>& frames) {
    ORC_LOG_DEBUG("Padding gaps in frame sequence...");
    
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
                ORC_LOG_DEBUG("Gap found: current VBI={}, next VBI={}, gap size={} frames",
                             current_vbi, next_vbi, gap_size);
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
                ORC_LOG_WARN("Large gap detected ({} frames), skipping padding (VBI {} to {})", 
                            gap_size, current_vbi, next_vbi);
            }
        }
    }
    
    frames = std::move(padded_frames);
    stats_.gaps_padded = gaps;
    stats_.padding_frames = total_padding;
    
    ORC_LOG_DEBUG("Padded {} gaps with {} total padding frames", gaps, total_padding);
}

void FieldMappingAnalyzer::renumber_for_pulldown(std::vector<FrameInfo>& frames) {
    ORC_LOG_DEBUG("Renumbering all frames to include pulldown frames...");
    
    int32_t new_vbi = frames.empty() ? 0 : frames[0].vbi_frame_number;
    for (auto& frame : frames) {
        frame.vbi_frame_number = new_vbi++;
    }
    
    ORC_LOG_DEBUG("Renumbering complete");
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
                // PAD directive is in fields (frames * 2)
                spec << "PAD_" << (pad_count * 2);
                first = false;
                pad_count = 0;
            }
            
            // Check if this frame is contiguous with the previous range
            bool is_contiguous = false;
            if (in_real_range && i > 0) {
                // Find the previous non-padded frame
                for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
                    if (!frames[j].is_padded) {
                        // Check if current frame's first field immediately follows previous frame's second field
                        uint64_t prev_second_field = frames[j].second_field.value();
                        uint64_t curr_first_field = frames[i].first_field.value();
                        is_contiguous = (curr_first_field == prev_second_field + 1);
                        break;
                    }
                }
            }
            
            // If not contiguous, close current range and start a new one
            if (in_real_range && !is_contiguous) {
                // Close previous range
                if (!first) spec << ",";
                // Find the last non-padded frame before this one
                for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
                    if (!frames[j].is_padded) {
                        uint64_t range_end_field = frames[j].second_field.value();
                        spec << current_range_start_field << "-" << range_end_field;
                        break;
                    }
                }
                first = false;
                in_real_range = false;
            }
            
            // Start new range if not currently in one
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
        // PAD directive is in fields (frames * 2)
        spec << "PAD_" << (pad_count * 2);
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
    rationale << "Input: " << stats.total_fields << " fields (" << stats.total_fields / 2 << " field pairs/frames)\n\n";
    
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
