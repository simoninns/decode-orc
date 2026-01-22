/*
 * File:        disc_mapper_analyzer.cpp
 * Module:      orc-core/analysis
 * Purpose:     Field mapping analyzer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "disc_mapper_analyzer.h"
#include "../analysis_progress.h"
#include "../../include/logging.h"
#include "../../include/observation_context.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <map>
#include <set>
#include <cmath>

namespace orc {

// ============================================================================
// Internal data structures for the disc mapping pipeline
// ============================================================================

/**
 * @brief Normalized field metadata extracted from VBI
 */
struct NormalizedField {
    FieldID field_id;
    FieldParity parity;
    VideoFormat format;
    
    // VBI-derived picture number (source of truth)
    std::optional<int32_t> picture_number;  // From CAV or CLV
    bool is_cav = false;                    // True if PN from CAV, false if from CLV
    int pn_confidence = 0;                  // 0-100
    
    // Phase information (supporting evidence only)
    std::optional<int32_t> phase;           // PAL: 1-8, NTSC: 1-4
    
    // Quality metrics for tie-breaking
    double quality_score = 0.0;
    
    // Flags
    bool is_lead_in_out = false;            // PN == 0
    bool is_invalid = false;                // Corrupt or unusable
};

/**
 * @brief Candidate frame pairing two fields
 */
struct CandidateFrame {
    FieldID first_field;
    FieldID second_field;
    
    std::optional<int32_t> picture_number;
    bool is_cav = false;
    int pn_confidence = 0;
    
    bool phase_valid = true;
    bool parity_valid = true;
    
    double quality_score = 0.0;
    
    bool is_lead_in_out = false;
    bool pn_disagreement = false;  // Fields have different PNs
};

/**
 * @brief Final mapped frame in the output sequence
 */
struct MappedFrame {
    std::optional<int32_t> picture_number;
    std::optional<FieldID> first_field;
    std::optional<FieldID> second_field;
    bool is_pad = false;  // Missing frame placeholder
};

// ============================================================================
// Helper functions
// ============================================================================

/**
 * @brief Decode BCD (Binary Coded Decimal) - same as BiphaseObserver
 */
static bool decode_bcd(uint32_t bcd, int32_t& output) {
    output = 0;
    int32_t multiplier = 1;
    
    while (bcd > 0) {
        uint32_t digit = bcd & 0x0F;
        if (digit > 9) {
            return false;  // Invalid BCD digit
        }
        output += digit * multiplier;
        multiplier *= 10;
        bcd >>= 4;
    }
    
    return true;
}

/**
 * @brief Decode CAV picture number from VBI data
 */
static std::optional<int32_t> decode_cav_picture_number(int32_t vbi17, int32_t vbi18) {
    // IEC 60857-1986 - 10.1.3 Picture numbers (CAV discs)
    // Check for CAV picture number on lines 17 and 18
    
    if ((vbi17 & 0xF00000) == 0xF00000) {
        int32_t pic_no;
        if (decode_bcd(vbi17 & 0x07FFFF, pic_no)) {
            return pic_no;
        }
    }
    
    if ((vbi18 & 0xF00000) == 0xF00000) {
        int32_t pic_no;
        if (decode_bcd(vbi18 & 0x07FFFF, pic_no)) {
            return pic_no;
        }
    }
    
    return std::nullopt;
}

/**
 * @brief Decode CLV timecode from VBI data and convert to picture number
 */
static std::optional<int32_t> decode_clv_picture_number(
    int32_t vbi16, int32_t vbi17, int32_t vbi18, VideoFormat format) {
    
    // IEC 60857-1986 - 10.1.6 Programme time code (CLV hours and minutes)
    int32_t hours = -1, minutes = -1;
    
    // Decode hours/minutes from line 17 or 18
    if ((vbi17 & 0xF0FF00) == 0xF0DD00) {
        int32_t h, m;
        if (decode_bcd((vbi17 & 0x0F0000) >> 16, h) &&
            decode_bcd(vbi17 & 0x0000FF, m)) {
            if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
                hours = h;
                minutes = m;
            }
        }
    }
    
    if (hours < 0 && (vbi18 & 0xF0FF00) == 0xF0DD00) {
        int32_t h, m;
        if (decode_bcd((vbi18 & 0x0F0000) >> 16, h) &&
            decode_bcd(vbi18 & 0x0000FF, m)) {
            if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
                hours = h;
                minutes = m;
            }
        }
    }
    
    // IEC 60857-1986 - 10.1.10 CLV picture number (seconds and frame within second)
    int32_t seconds = -1, picture = -1;
    
    if ((vbi16 & 0xF0F000) == 0x80E000) {
        uint32_t tens = (vbi16 & 0x0F0000) >> 16;
        int32_t sec_digit, pic_no;
        
        if (tens >= 0xA && tens <= 0xF &&
            decode_bcd((vbi16 & 0x000F00) >> 8, sec_digit) &&
            decode_bcd(vbi16 & 0x0000FF, pic_no)) {
            
            int32_t sec = (10 * (tens - 0xA)) + sec_digit;
            
            if (sec >= 0 && sec <= 59 && pic_no >= 0 && pic_no <= 29) {
                seconds = sec;
                picture = pic_no;
            }
        }
    }
    
    // Only compute picture number if all components are valid
    if (hours >= 0 && minutes >= 0 && seconds >= 0 && picture >= 0) {
        int fps = (format == VideoFormat::PAL) ? 25 : 30;
        int32_t total_seconds = hours * 3600 + minutes * 60 + seconds;
        int32_t pn = total_seconds * fps + picture;
        return pn > 0 ? std::optional<int32_t>(pn) : std::nullopt;
    }
    
    return std::nullopt;
}

/**
 * @brief Check for lead-in/lead-out codes
 */
static bool is_lead_in_out(int32_t vbi17, int32_t vbi18) {
    // IEC 60857-1986 - 10.1.1 Lead-in, 10.1.2 Lead-out
    return (vbi17 == 0x88FFFF || vbi18 == 0x88FFFF ||  // Lead-in
            vbi17 == 0x80EEEE || vbi18 == 0x80EEEE);   // Lead-out
}

/**
 * @brief Normalize a single field's metadata using VBI bytes from ObservationContext
 */
static NormalizedField normalize_field(
    const VideoFieldRepresentation& source,
    const ObservationContext& obs_context,
    FieldID field_id) {
    
    NormalizedField nf;
    nf.field_id = field_id;
    
    // Get basic descriptor
    auto desc = source.get_descriptor(field_id);
    if (!desc) {
        nf.is_invalid = true;
        return nf;
    }
    
    nf.parity = desc->parity;
    nf.format = desc->format;
    
    // Get phase hint
    auto phase_hint = source.get_field_phase_hint(field_id);
    if (phase_hint && phase_hint->field_phase_id > 0) {
        nf.phase = phase_hint->field_phase_id;
    }
    
    // Get VBI bytes from ObservationContext (populated by observers)
    auto vbi16_opt = obs_context.get(field_id, "biphase", "vbi_line_16");
    auto vbi17_opt = obs_context.get(field_id, "biphase", "vbi_line_17");
    auto vbi18_opt = obs_context.get(field_id, "biphase", "vbi_line_18");
    
    if (vbi16_opt && vbi17_opt && vbi18_opt) {
        int32_t vbi16 = std::get<int32_t>(*vbi16_opt);
        int32_t vbi17 = std::get<int32_t>(*vbi17_opt);
        int32_t vbi18 = std::get<int32_t>(*vbi18_opt);
        
        ORC_LOG_DEBUG("Field {}: VBI data: {:08x} {:08x} {:08x}", 
                     field_id.value(), vbi16, vbi17, vbi18);
        
        // Check for lead-in/out first
        if (is_lead_in_out(vbi17, vbi18)) {
            nf.is_lead_in_out = true;
            ORC_LOG_DEBUG("Field {}: Detected lead-in/out", field_id.value());
        }
        
        // Try CAV picture number (priority 1)
        auto cav_pn = decode_cav_picture_number(vbi17, vbi18);
        if (cav_pn) {
            nf.picture_number = cav_pn;
            nf.is_cav = true;
            nf.pn_confidence = 95;
            
            ORC_LOG_DEBUG("Field {}: CAV picture number = {}", field_id.value(), *cav_pn);
            
            if (*cav_pn == 0) {
                nf.is_lead_in_out = true;
            }
        }
        // Try CLV timecode (priority 2)
        else {
            auto clv_pn = decode_clv_picture_number(vbi16, vbi17, vbi18, nf.format);
            if (clv_pn) {
                nf.picture_number = clv_pn;
                nf.is_cav = false;
                nf.pn_confidence = 85;
                
                ORC_LOG_DEBUG("Field {}: CLV picture number = {}", field_id.value(), *clv_pn);
            } else {
                ORC_LOG_DEBUG("Field {}: Failed to decode CLV timecode", field_id.value());
            }
        }
    } else {
        ORC_LOG_DEBUG("Field {}: No VBI data in ObservationContext", field_id.value());
    }
    
    // Compute quality score (placeholder)
    nf.quality_score = 100.0;
    
    return nf;
}

/**
 * @brief Convert picture number to CLV timecode string
 */
static std::string picture_number_to_timecode(int32_t pn, VideoFormat format) {
    int32_t fps = (format == VideoFormat::PAL) ? 25 : 30;
    
    int32_t f = pn % fps;
    int32_t total_seconds = pn / fps;
    int32_t s = total_seconds % 60;
    int32_t total_minutes = total_seconds / 60;
    int32_t m = total_minutes % 60;
    int32_t h = total_minutes / 60;
    
    std::ostringstream tc;
    tc << h << ":" 
       << std::setfill('0') << std::setw(2) << m << ":"
       << std::setfill('0') << std::setw(2) << s << "."
       << std::setfill('0') << std::setw(2) << f;
    
    return tc.str();
}

/**
 * @brief Generate a compact visual representation of picture numbers in frame list
 */
static std::string generate_frame_map(const std::vector<CandidateFrame>& frames, 
                                      bool is_clv, VideoFormat format) {
    if (frames.empty()) return "(empty)";
    
    // Extract picture numbers that exist
    std::vector<int32_t> pns;
    for (const auto& frame : frames) {
        if (frame.picture_number) {
            pns.push_back(*frame.picture_number);
        }
    }
    
    if (pns.empty()) return "(no picture numbers)";
    
    // Sort
    std::sort(pns.begin(), pns.end());
    
    // Build range notation
    std::ostringstream result;
    size_t i = 0;
    while (i < pns.size()) {
        int32_t start = pns[i];
        int32_t end = start;
        size_t j = i + 1;
        
        // Look for consecutive numbers
        while (j < pns.size() && pns[j] == end + 1) {
            end = pns[j];
            j++;
        }
        
        if (i > 0) result << ",";
        
        if (is_clv) {
            // Use timecode format for CLV
            if (j - i >= 3) {
                result << picture_number_to_timecode(start, format) 
                       << "-" << picture_number_to_timecode(end, format);
            } else {
                for (size_t k = i; k < j; k++) {
                    if (k > i) result << ",";
                    result << picture_number_to_timecode(pns[k], format);
                }
            }
        } else {
            // Use picture numbers for CAV
            if (j - i >= 3) {
                result << start << "-" << end;
            } else {
                for (size_t k = i; k < j; k++) {
                    if (k > i) result << ",";
                    result << pns[k];
                }
            }
        }
        
        i = j;
    }
    
    return result.str();
}

/**
 * @brief Generate frame map for MappedFrame vector (includes PAD markers)
 */
static std::string generate_frame_map(const std::vector<MappedFrame>& frames,
                                      bool is_clv, VideoFormat format) {
    if (frames.empty()) return "(empty)";
    
    // Build a list with picture numbers (or PAD markers)
    struct Entry {
        bool is_pad;
        std::optional<int32_t> pn;
    };
    
    std::vector<Entry> entries;
    for (const auto& frame : frames) {
        if (frame.is_pad) {
            entries.push_back({true, std::nullopt});
        } else if (frame.picture_number) {
            entries.push_back({false, *frame.picture_number});
        }
    }
    
    if (entries.empty()) return "(no entries)";
    
    // Build range notation with PAD support
    std::ostringstream result;
    size_t i = 0;
    while (i < entries.size()) {
        // Handle PAD sequences
        if (entries[i].is_pad) {
            size_t pad_count = 1;
            while (i + pad_count < entries.size() && entries[i + pad_count].is_pad) {
                pad_count++;
            }
            
            if (i > 0) result << ",";
            if (pad_count == 1) {
                result << "PAD";
            } else {
                result << "PAD(" << pad_count << ")";
            }
            
            i += pad_count;
            continue;
        }
        
        // Handle consecutive picture number sequences
        int32_t start_pn = *entries[i].pn;
        int32_t end_pn = start_pn;
        size_t j = i + 1;
        
        // Look for consecutive picture numbers
        while (j < entries.size() && !entries[j].is_pad && entries[j].pn) {
            if (*entries[j].pn == end_pn + 1) {
                end_pn = *entries[j].pn;
                j++;
            } else {
                break;
            }
        }
        
        if (i > 0) result << ",";
        
        if (is_clv) {
            // Use timecode format for CLV
            if (j - i >= 3) {
                result << picture_number_to_timecode(start_pn, format) 
                       << "-" << picture_number_to_timecode(end_pn, format);
            } else {
                for (size_t k = i; k < j; k++) {
                    if (k > i) result << ",";
                    result << picture_number_to_timecode(*entries[k].pn, format);
                }
            }
        } else {
            // Use picture numbers for CAV
            if (j - i >= 3) {
                result << start_pn << "-" << end_pn;
            } else {
                for (size_t k = i; k < j; k++) {
                    if (k > i) result << ",";
                    result << *entries[k].pn;
                }
            }
        }
        
        i = j;
    }
    
    return result.str();
}

/**
 * @brief Check if phase sequence is valid for a frame pair
 */
static bool is_phase_valid(
    const std::optional<int32_t>& phase1,
    const std::optional<int32_t>& phase2,
    VideoFormat format) {
    
    if (!phase1 || !phase2) return true;  // Can't validate without phase
    
    if (format == VideoFormat::PAL) {
        // PAL: 8-field sequence, expect increment by 2
        int expected_phase2 = (*phase1 % 8) + 2;
        if (expected_phase2 > 8) expected_phase2 -= 8;
        return *phase2 == expected_phase2 || *phase2 == expected_phase2 - 1 || *phase2 == expected_phase2 + 1;
    } else if (format == VideoFormat::NTSC) {
        // NTSC: 4-field sequence, expect increment by 2
        int expected_phase2 = (*phase1 % 4) + 2;
        if (expected_phase2 > 4) expected_phase2 -= 4;
        return *phase2 == expected_phase2 || *phase2 == expected_phase2 - 1 || *phase2 == expected_phase2 + 1;
    }
    
    return true;
}

/**
 * @brief Create candidate frame from two normalized fields
 */
static std::optional<CandidateFrame> pair_fields(
    const NormalizedField& f1,
    const NormalizedField& f2) {
    
    CandidateFrame frame;
    frame.first_field = f1.field_id;
    frame.second_field = f2.field_id;
    
    // Select picture number using priority:
    // 1. First field CAV
    // 2. Second field CAV  
    // 3. First field CLV
    // 4. Second field CLV
    if (f1.picture_number && f1.is_cav) {
        frame.picture_number = f1.picture_number;
        frame.is_cav = true;
        frame.pn_confidence = f1.pn_confidence;
    } else if (f2.picture_number && f2.is_cav) {
        frame.picture_number = f2.picture_number;
        frame.is_cav = true;
        frame.pn_confidence = f2.pn_confidence;
    } else if (f1.picture_number && !f1.is_cav) {
        frame.picture_number = f1.picture_number;
        frame.is_cav = false;
        frame.pn_confidence = f1.pn_confidence;
    } else if (f2.picture_number && !f2.is_cav) {
        frame.picture_number = f2.picture_number;
        frame.is_cav = false;
        frame.pn_confidence = f2.pn_confidence;
    }
    
    // Check for PN disagreement
    if (f1.picture_number && f2.picture_number && 
        *f1.picture_number != *f2.picture_number) {
        frame.pn_disagreement = true;
    }
    
    // Check phase validity
    frame.phase_valid = is_phase_valid(f1.phase, f2.phase, f1.format);
    
    // Check parity
    frame.parity_valid = (f1.parity != f2.parity);
    
    // Combine quality scores
    frame.quality_score = (f1.quality_score + f2.quality_score) / 2.0;
    
    // Check for lead-in/out
    if (f1.is_lead_in_out || f2.is_lead_in_out) {
        frame.is_lead_in_out = true;
    }
    
    return frame;
}

// ============================================================================
// Main analysis implementation
// ============================================================================

FieldMappingDecision DiscMapperAnalyzer::analyze(
    const VideoFieldRepresentation& source,
    const ObservationContext& observation_context,
    const Options& options,
    AnalysisProgress* progress) {
    
    FieldMappingDecision decision;
    std::ostringstream rationale;
    
    // Get field range
    auto range = source.field_range();
    size_t total_fields = range.size();
    
    decision.stats.total_fields = total_fields;
    
    if (total_fields == 0) {
        decision.success = false;
        decision.rationale = "No fields found in source";
        return decision;
    }
    
    rationale << "=== Disc Mapping Analysis ===\n\n";
    rationale << "Input: " << total_fields << " fields\n\n";
    
    // ========================================================================
    // Stage 1: Per-field VBI normalization
    // ========================================================================
    
    if (progress) {
        progress->setStatus("Normalizing field metadata...");
        progress->setProgress(20);
    }
    
    std::vector<NormalizedField> normalized_fields;
    normalized_fields.reserve(total_fields);
    
    size_t fields_with_pn = 0;
    size_t cav_fields = 0;
    size_t clv_fields = 0;
    
    for (FieldID fid = range.start; fid < range.end; fid = FieldID(fid.value() + 1)) {
        auto nf = normalize_field(source, observation_context, fid);
        
        if (nf.picture_number) {
            fields_with_pn++;
            if (nf.is_cav) cav_fields++;
            else clv_fields++;
        }
        
        normalized_fields.push_back(nf);
    }
    
    // Determine disc type
    decision.is_cav = (cav_fields > clv_fields);
    
    // Determine video format
    auto first_desc = source.get_descriptor(range.start);
    if (first_desc) {
        decision.is_pal = (first_desc->format == VideoFormat::PAL);
    }
    
    rationale << "Stage 1: VBI Normalization\n";
    rationale << "  Fields with picture numbers: " << fields_with_pn << " / " << total_fields << "\n";
    rationale << "  CAV fields: " << cav_fields << "\n";
    rationale << "  CLV fields: " << clv_fields << "\n";
    rationale << "  Detected format: " << (decision.is_pal ? "PAL" : "NTSC") << "\n";
    rationale << "  Detected disc type: " << (decision.is_cav ? "CAV" : "CLV") << "\n\n";
    
    if (progress && progress->isCancelled()) {
        decision.success = false;
        decision.rationale = "Analysis cancelled by user";
        return decision;
    }
    
    // ========================================================================
    // Stage 2: Field pairing (candidate frame generation)
    // ========================================================================
    
    if (progress) {
        progress->setStatus("Pairing fields into frames...");
        progress->setProgress(40);
    }
    
    std::vector<CandidateFrame> candidate_frames;
    
    // Simple sequential pairing
    for (size_t i = 0; i + 1 < normalized_fields.size(); i += 2) {
        auto frame_opt = pair_fields(normalized_fields[i], normalized_fields[i + 1]);
        if (frame_opt) {
            candidate_frames.push_back(*frame_opt);
        }
    }
    
    rationale << "Stage 2: Field Pairing\n";
    rationale << "  Candidate frames created: " << candidate_frames.size() << "\n";
    VideoFormat fmt = decision.is_pal ? VideoFormat::PAL : VideoFormat::NTSC;
    rationale << "  Frame map: " << generate_frame_map(candidate_frames, !decision.is_cav, fmt) << "\n\n";
    
    if (progress && progress->isCancelled()) {
        decision.success = false;
        decision.rationale = "Analysis cancelled by user";
        return decision;
    }
    
    // ========================================================================
    // Stage 3: Frame validation and filtering
    // ========================================================================
    
    if (progress) {
        progress->setStatus("Validating frames...");
        progress->setProgress(60);
    }
    
    std::vector<CandidateFrame> valid_frames;
    size_t removed_lead_in_out = 0;
    size_t removed_invalid_phase = 0;
    
    for (const auto& frame : candidate_frames) {
        // Drop lead-in/out frames
        if (frame.is_lead_in_out) {
            removed_lead_in_out++;
            continue;
        }
        
        // Drop phase-invalid frames only if no PN or low confidence
        if (!frame.phase_valid && (!frame.picture_number || frame.pn_confidence < 50)) {
            removed_invalid_phase++;
            continue;
        }
        
        valid_frames.push_back(frame);
    }
    
    decision.stats.removed_lead_in_out = removed_lead_in_out;
    decision.stats.removed_invalid_phase = removed_invalid_phase;
    
    rationale << "Stage 3: Frame Validation\n";
    rationale << "  Frames after filtering: " << valid_frames.size() << "\n";
    rationale << "  Removed (lead-in/out): " << removed_lead_in_out << "\n";
    rationale << "  Removed (invalid phase): " << removed_invalid_phase << "\n";
    rationale << "  Frame map: " << generate_frame_map(valid_frames, !decision.is_cav, fmt) << "\n\n";
    
    if (progress && progress->isCancelled()) {
        decision.success = false;
        decision.rationale = "Analysis cancelled by user";
        return decision;
    }
    
    // ========================================================================
    // Stage 4: Deduplication by picture number
    // ========================================================================
    
    if (progress) {
        progress->setStatus("Deduplicating frames...");
        progress->setProgress(75);
    }
    
    // Group frames by PN
    std::map<int32_t, std::vector<CandidateFrame>> frames_by_pn;
    std::vector<CandidateFrame> frames_without_pn;
    
    for (const auto& frame : valid_frames) {
        if (frame.picture_number) {
            frames_by_pn[*frame.picture_number].push_back(frame);
        } else {
            frames_without_pn.push_back(frame);
        }
    }
    
    // Select best frame for each PN
    std::vector<CandidateFrame> selected_frames;
    size_t removed_duplicates = 0;
    
    for (const auto& [pn, frames] : frames_by_pn) {
        if (frames.size() == 1) {
            selected_frames.push_back(frames[0]);
        } else {
            // Multiple frames with same PN - select best
            removed_duplicates += frames.size() - 1;
            
            auto best = std::max_element(frames.begin(), frames.end(),
                [](const CandidateFrame& a, const CandidateFrame& b) {
                    // Priority: confidence > phase_valid > quality
                    if (a.pn_confidence != b.pn_confidence) 
                        return a.pn_confidence < b.pn_confidence;
                    if (a.phase_valid != b.phase_valid) 
                        return !a.phase_valid;
                    return a.quality_score < b.quality_score;
                });
            
            selected_frames.push_back(*best);
        }
    }
    
    decision.stats.removed_duplicates = removed_duplicates;
    
    rationale << "Stage 4: Deduplication\n";
    rationale << "  Unique picture numbers: " << frames_by_pn.size() << "\n";
    rationale << "  Duplicates removed: " << removed_duplicates << "\n";
    rationale << "  Frames without PN: " << frames_without_pn.size() << "\n";
    rationale << "  Frame map: " << generate_frame_map(selected_frames, !decision.is_cav, fmt) << "\n\n";
    
    if (progress && progress->isCancelled()) {
        decision.success = false;
        decision.rationale = "Analysis cancelled by user";
        return decision;
    }
    
    // ========================================================================
    // Stage 5: Sort by PN and detect gaps
    // ========================================================================
    
    if (progress) {
        progress->setStatus("Detecting gaps and building timeline...");
        progress->setProgress(85);
    }
    
    // Sort selected frames by PN
    std::sort(selected_frames.begin(), selected_frames.end(),
        [](const CandidateFrame& a, const CandidateFrame& b) {
            if (!a.picture_number) return true;
            if (!b.picture_number) return false;
            return *a.picture_number < *b.picture_number;
        });
    
    // Build final mapped sequence with PAD insertion
    std::vector<MappedFrame> final_frames;
    size_t gaps_padded = 0;
    size_t padding_frames = 0;
    
    std::optional<int32_t> prev_pn;
    size_t frames_without_pn_included = 0;
    
    for (const auto& frame : selected_frames) {
        // Handle frames WITH picture numbers
        if (frame.picture_number) {
            int32_t current_pn = *frame.picture_number;
            
            // Check for gap
            if (prev_pn && current_pn > *prev_pn + 1) {
                if (options.pad_gaps) {
                    // Insert PAD frames
                    for (int32_t missing_pn = *prev_pn + 1; missing_pn < current_pn; ++missing_pn) {
                        MappedFrame pad;
                        pad.picture_number = missing_pn;
                        pad.is_pad = true;
                        final_frames.push_back(pad);
                        padding_frames++;
                        ORC_LOG_DEBUG("Inserted PAD frame for missing picture number {}", missing_pn);
                    }
                    gaps_padded++;
                }
            }
            
            // Add current frame
            MappedFrame mapped;
            mapped.picture_number = frame.picture_number;
            mapped.first_field = frame.first_field;
            mapped.second_field = frame.second_field;
            final_frames.push_back(mapped);
            
            prev_pn = current_pn;
        } else {
            // Frames without PN - include them but don't use for gap detection
            // (Section 11: "Do not invent PN" but still include for continuity)
            MappedFrame mapped;
            mapped.first_field = frame.first_field;
            mapped.second_field = frame.second_field;
            final_frames.push_back(mapped);
            frames_without_pn_included++;
        }
    }
    
    decision.stats.gaps_padded = gaps_padded;
    decision.stats.padding_frames = padding_frames;
    
    rationale << "Stage 5: Gap Detection and Timeline Construction\n";
    rationale << "  Gaps detected: " << gaps_padded << "\n";
    rationale << "  PAD frames inserted: " << padding_frames << "\n";
    rationale << "  Frames without PN (included): " << frames_without_pn_included << "\n";
    rationale << "  Final frame count: " << final_frames.size() << "\n";
    rationale << "  Frame map: " << generate_frame_map(final_frames, !decision.is_cav, fmt) << "\n\n";
    
    // ========================================================================
    // Stage 6: Generate mapping specification with range notation
    // ========================================================================
    
    if (progress) {
        progress->setStatus("Generating mapping specification...");
        progress->setProgress(95);
    }
    
    // First, flatten frames into a list of field IDs or PAD markers
    std::vector<std::string> field_list;
    for (const auto& frame : final_frames) {
        if (frame.is_pad) {
            // Each PAD frame represents 2 fields
            field_list.push_back("PAD");
            field_list.push_back("PAD");
            ORC_LOG_DEBUG("Adding PAD frame to field list (picture_number={})", 
                         frame.picture_number ? std::to_string(*frame.picture_number) : "none");
        } else {
            if (frame.first_field) {
                field_list.push_back(std::to_string(frame.first_field->value()));
            }
            if (frame.second_field) {
                field_list.push_back(std::to_string(frame.second_field->value()));
            }
        }
    }
    
    ORC_LOG_DEBUG("Field list size: {}, final_frames size: {}", field_list.size(), final_frames.size());
    
    // Now collapse consecutive sequences into ranges
    std::ostringstream spec;
    size_t i = 0;
    while (i < field_list.size()) {
        const auto& current = field_list[i];
        
        // Handle PAD sequences
        if (current == "PAD") {
            size_t pad_count = 1;
            while (i + pad_count < field_list.size() && field_list[i + pad_count] == "PAD") {
                pad_count++;
            }
            
            if (i > 0) spec << ",";
            if (pad_count == 1) {
                spec << "PAD";
            } else {
                spec << "PAD_" << pad_count;
            }
            
            i += pad_count;
            continue;
        }
        
        // Try to parse as number
        int start_num = std::stoi(current);
        int end_num = start_num;
        size_t j = i + 1;
        
        // Look ahead for consecutive numbers
        while (j < field_list.size() && field_list[j] != "PAD") {
            int next_num = std::stoi(field_list[j]);
            if (next_num == end_num + 1) {
                end_num = next_num;
                j++;
            } else {
                break;
            }
        }
        
        // Output range or single number
        if (i > 0) spec << ",";
        if (j - i >= 3) {
            // Use range notation for 3+ consecutive numbers
            spec << start_num << "-" << end_num;
        } else {
            // Output individual numbers
            for (size_t k = i; k < j; k++) {
                if (k > i) spec << ",";
                spec << field_list[k];
            }
        }
        
        i = j;
    }
    
    decision.mapping_spec = spec.str();
    
    decision.rationale = rationale.str();
    decision.success = true;
    
    if (progress) {
        progress->setStatus("Analysis complete");
        progress->setProgress(100);
    }
    
    ORC_LOG_INFO("Disc mapping analysis complete: {} frames ({} PAD)", 
                 final_frames.size(), padding_frames);
    
    return decision;
}

} // namespace orc

