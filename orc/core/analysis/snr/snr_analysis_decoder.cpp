/*
 * File:        snr_analysis_decoder.cpp
 * Module:      orc-core
 * Purpose:     SNR analysis data extraction implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "snr_analysis_decoder.h"
#include "../../include/logging.h"
#include "../../include/observation_context.h"
#include "../../include/observation_cache.h"
#include "../../include/video_field_representation.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace orc {

SNRAnalysisDecoder::SNRAnalysisDecoder(std::shared_ptr<const DAG> dag)
    : dag_(dag)
{
    if (!dag) {
        throw std::invalid_argument("SNRAnalysisDecoder: DAG cannot be null");
    }
    ORC_LOG_DEBUG("SNRAnalysisDecoder: Created");
}

void SNRAnalysisDecoder::update_dag(std::shared_ptr<const DAG> dag)
{
    if (!dag) {
        throw std::invalid_argument("SNRAnalysisDecoder: DAG cannot be null");
    }
    dag_ = dag;
    ORC_LOG_DEBUG("SNRAnalysisDecoder: DAG updated");
}

void SNRAnalysisDecoder::set_observation_cache(std::shared_ptr<ObservationCache> cache)
{
    if (!cache) {
        throw std::invalid_argument("SNRAnalysisDecoder: ObservationCache cannot be null");
    }
    obs_cache_ = cache;
    ORC_LOG_DEBUG("SNRAnalysisDecoder: Observation cache updated");
}

// Helper: Calculate signal variance for a line
static double calculate_variance(const std::vector<uint16_t>& samples) {
    if (samples.empty()) return 0.0;
    
    double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    double variance = 0.0;
    for (uint16_t sample : samples) {
        double diff = sample - mean;
        variance += diff * diff;
    }
    return variance / samples.size();
}

// Helper: Convert linear variance to dB
static double to_db(double linear_value) {
    if (linear_value <= 0.0) return -100.0;
    return 20.0 * std::log10(linear_value);
}

std::optional<FieldSNRStats> SNRAnalysisDecoder::get_snr_for_field(
    NodeID node_id,
    FieldID field_id,
    SNRAnalysisMode mode)
{
    try {
        if (!obs_cache_) {
            ORC_LOG_WARN("SNRAnalysisDecoder: No observation cache available");
            return std::nullopt;
        }
        
        FieldSNRStats stats;
        stats.field_id = field_id;
        stats.has_data = false;
        
        // Get rendered field representation from cache
        auto field_opt = obs_cache_->get_field(node_id, field_id);
        if (!field_opt) {
            ORC_LOG_DEBUG("SNRAnalysisDecoder: Field {} not available for rendering", field_id.value());
            return stats;
        }
        
        auto field = field_opt.value();
        auto descriptor = field->get_descriptor(field_id);
        if (!descriptor) {
            ORC_LOG_WARN("SNRAnalysisDecoder: No descriptor for field {}", field_id.value());
            return stats;
        }
        
        // Get active line range hint for determining signal vs noise regions
        auto active_hint = field->get_active_line_hint();
        
        std::vector<double> white_levels;   // For white SNR
        std::vector<double> black_levels;   // For black PSNR
        
        // Sample a few lines from the active video region and blanking region
        // Typical VBI/blanking is lines 1-20 and beyond active area
        // For NTSC: ~243 lines per field, active 21-243 approx
        // For PAL: ~312 lines per field, active 24-312 approx
        
        int first_blanking = 1;
        int last_blanking = std::min(20, (int)descriptor->height / 4);  // Blanking at top and bottom
        
        int first_active = first_blanking + 3;  // Skip very top
        int last_active = descriptor->height - 3;  // Skip very bottom
        
        if (active_hint && active_hint->is_valid()) {
            first_active = active_hint->first_active_field_line;
            last_active = active_hint->last_active_field_line;
        }
        
        // Collect white-level samples from blanking region (high signal level)
        // Blanking should be around black burst level
        if (mode == SNRAnalysisMode::WHITE || mode == SNRAnalysisMode::BOTH) {
            for (int line = first_active; line < std::min(first_active + 5, last_active); ++line) {
                auto line_data = field->get_line(field_id, line);
                if (line_data) {
                    std::vector<uint16_t> samples(line_data, line_data + descriptor->width);
                    double var = calculate_variance(samples);
                    white_levels.push_back(var);
                }
            }
        }
        
        // Collect black-level samples from blanking region
        if (mode == SNRAnalysisMode::BLACK || mode == SNRAnalysisMode::BOTH) {
            for (int line = first_blanking; line < std::min(first_blanking + 3, last_blanking); ++line) {
                auto line_data = field->get_line(field_id, line);
                if (line_data) {
                    std::vector<uint16_t> samples(line_data, line_data + descriptor->width);
                    double var = calculate_variance(samples);
                    black_levels.push_back(var);
                }
            }
        }
        
        // Calculate SNR/PSNR values
        if (!white_levels.empty() && (mode == SNRAnalysisMode::WHITE || mode == SNRAnalysisMode::BOTH)) {
            double avg_white_variance = std::accumulate(white_levels.begin(), white_levels.end(), 0.0) 
                                       / white_levels.size();
            stats.white_snr = to_db(avg_white_variance);
            stats.has_white_snr = true;
        }
        
        if (!black_levels.empty() && (mode == SNRAnalysisMode::BLACK || mode == SNRAnalysisMode::BOTH)) {
            double avg_black_variance = std::accumulate(black_levels.begin(), black_levels.end(), 0.0) 
                                       / black_levels.size();
            stats.black_psnr = to_db(avg_black_variance);
            stats.has_black_psnr = true;
        }
        
        // Extract frame number if available
        if (descriptor->frame_number) {
            stats.frame_number = descriptor->frame_number.value();
        }
        
        stats.has_data = (stats.has_white_snr || stats.has_black_psnr);
        ORC_LOG_DEBUG("SNRAnalysisDecoder: Field {} SNR: white={:.2f}dB, black={:.2f}dB",
                     field_id.value(), stats.white_snr, stats.black_psnr);
        return stats;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("SNRAnalysisDecoder: Exception getting SNR for field {}: {}",
                     field_id.value(), e.what());
        return std::nullopt;
    }
}

std::vector<FieldSNRStats> SNRAnalysisDecoder::get_snr_for_all_fields(
    NodeID node_id,
    SNRAnalysisMode mode,
    size_t max_fields,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    std::vector<FieldSNRStats> results;
    
    try {
        if (!obs_cache_) {
            ORC_LOG_ERROR("SNRAnalysisDecoder: No observation cache available");
            return results;
        }
        
        ORC_LOG_DEBUG("SNRAnalysisDecoder: Processing SNR analysis for node '{}'",
                     node_id.to_string());
        
        // Get total field count at this node
        size_t total_fields = obs_cache_->get_field_count(node_id);
        if (total_fields == 0) {
            ORC_LOG_WARN("SNRAnalysisDecoder: No fields available at node '{}'",
                        node_id.to_string());
            return results;
        }
        
        // Limit to max_fields if specified
        if (max_fields > 0 && max_fields < total_fields) {
            total_fields = max_fields;
        }
        
        ORC_LOG_DEBUG("SNRAnalysisDecoder: Processing {} fields", total_fields);
        
        // Process each field
        for (size_t i = 0; i < total_fields; ++i) {
            FieldID fid(i);
            auto stats_opt = get_snr_for_field(node_id, fid, mode);
            if (stats_opt) {
                results.push_back(stats_opt.value());
            }
            
            // Progress callback
            if (progress_callback) {
                progress_callback(i + 1, total_fields, "Processing field " + std::to_string(i));
            }
        }
        
        ORC_LOG_DEBUG("SNRAnalysisDecoder: Processed {} fields", results.size());
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("SNRAnalysisDecoder: Exception processing all fields: {}", e.what());
    }
    
    return results;
}

std::vector<FrameSNRStats> SNRAnalysisDecoder::get_snr_by_frames(
    NodeID node_id,
    SNRAnalysisMode mode,
    size_t max_frames,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    std::vector<FrameSNRStats> results;
    
    try {
        ORC_LOG_DEBUG("SNRAnalysisDecoder: Processing SNR analysis by frames for node '{}'",
                     node_id.to_string());
        
        // First get all field stats
        auto field_stats = get_snr_for_all_fields(node_id, mode, max_frames * 2, nullptr);
        
        if (field_stats.empty()) {
            ORC_LOG_DEBUG("SNRAnalysisDecoder: No field stats available");
            return results;
        }
        
        // Group fields into frames (2 fields per frame)
        std::map<int32_t, std::vector<FieldSNRStats>> frames_map;
        for (const auto& field_stat : field_stats) {
            int32_t frame_number = 1;  // Default frame number
            if (field_stat.frame_number) {
                frame_number = field_stat.frame_number.value();
            } else {
                // Estimate frame number from field index
                frame_number = (field_stat.field_id.value() / 2) + 1;
            }
            frames_map[frame_number].push_back(field_stat);
        }
        
        // Aggregate into frame-based stats
        size_t frame_count = 0;
        for (const auto& [frame_number, fields] : frames_map) {
            if (max_frames > 0 && frame_count >= max_frames) {
                break;
            }
            
            FrameSNRStats frame_stat;
            frame_stat.frame_number = frame_number;
            frame_stat.white_snr = 0.0;
            frame_stat.black_psnr = 0.0;
            frame_stat.has_white_snr = false;
            frame_stat.has_black_psnr = false;
            frame_stat.has_data = false;
            frame_stat.field_count = 0;
            
            // Accumulate stats from both fields
            for (const auto& field_stat : fields) {
                if (field_stat.has_white_snr) {
                    frame_stat.white_snr += field_stat.white_snr;
                    frame_stat.has_white_snr = true;
                }
                if (field_stat.has_black_psnr) {
                    frame_stat.black_psnr += field_stat.black_psnr;
                    frame_stat.has_black_psnr = true;
                }
                if (field_stat.has_data) {
                    frame_stat.has_data = true;
                }
                frame_stat.field_count++;
            }
            
            // Average the values if we have data from multiple fields
            if (frame_stat.has_white_snr && frame_stat.field_count > 0) {
                frame_stat.white_snr /= frame_stat.field_count;
            }
            if (frame_stat.has_black_psnr && frame_stat.field_count > 0) {
                frame_stat.black_psnr /= frame_stat.field_count;
            }
            
            results.push_back(frame_stat);
            frame_count++;
            
            // Progress callback
            if (progress_callback) {
                progress_callback(frame_count, frames_map.size(), 
                                "Processing frame " + std::to_string(frame_number));
            }
        }
        
        ORC_LOG_DEBUG("SNRAnalysisDecoder: Processed {} frames", results.size());
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("SNRAnalysisDecoder: Exception processing frames: {}", e.what());
    }
    
    return results;
}

} // namespace orc
