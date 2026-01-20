/*
 * File:        burst_level_analysis_decoder.cpp
 * Module:      orc-core
 * Purpose:     Burst level analysis data extraction implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "burst_level_analysis_decoder.h"
#include "../../include/logging.h"
#include "../../include/observation_context.h"
#include "../../include/observation_cache.h"
#include "../../include/video_field_representation.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace orc {

BurstLevelAnalysisDecoder::BurstLevelAnalysisDecoder(std::shared_ptr<const DAG> dag)
    : dag_(dag)
{
    if (!dag) {
        throw std::invalid_argument("BurstLevelAnalysisDecoder: DAG cannot be null");
    }
    ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Created");
}

void BurstLevelAnalysisDecoder::update_dag(std::shared_ptr<const DAG> dag)
{
    if (!dag) {
        throw std::invalid_argument("BurstLevelAnalysisDecoder: DAG cannot be null");
    }
    dag_ = dag;
    ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: DAG updated");
}

void BurstLevelAnalysisDecoder::set_observation_cache(std::shared_ptr<ObservationCache> cache)
{
    if (!cache) {
        throw std::invalid_argument("BurstLevelAnalysisDecoder: ObservationCache cannot be null");
    }
    obs_cache_ = cache;
    ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Observation cache updated");
}

// Helper: Convert IRE value from 16-bit sample
// Composite video in 16-bit: 0=blank, ~2300=black, ~16384=white (std values)
// IRE: -40 to +100, where 0=black, 100=white
static double sample_to_ire(uint16_t sample) {
    // Typical TBC values:
    // 0 = blank (-40 IRE)
    // ~2300 = black (0 IRE)
    // ~16384 = white (100 IRE)
    // Linear scaling: IRE = (sample - 2300) * 100 / 14084
    
    const double BLACK_SAMPLE = 2300.0;
    const double WHITE_SAMPLE = 16384.0;
    const double RANGE = WHITE_SAMPLE - BLACK_SAMPLE;
    
    double ire = ((double)sample - BLACK_SAMPLE) * 100.0 / RANGE;
    return ire;
}

// Helper: Calculate median amplitude of signal section
static double calculate_median_amplitude(const std::vector<uint16_t>& samples) {
    if (samples.empty()) return 0.0;
    
    std::vector<uint16_t> sorted_samples = samples;
    std::sort(sorted_samples.begin(), sorted_samples.end());
    
    if (sorted_samples.size() % 2 == 0) {
        // Even number of elements: average of two middle values
        size_t mid = sorted_samples.size() / 2;
        return (sorted_samples[mid - 1] + sorted_samples[mid]) / 2.0;
    } else {
        // Odd number of elements: middle value
        return sorted_samples[sorted_samples.size() / 2];
    }
}

std::optional<FieldBurstLevelStats> BurstLevelAnalysisDecoder::get_burst_level_for_field(
    NodeID node_id,
    FieldID field_id)
{
    try {
        if (!obs_cache_) {
            ORC_LOG_WARN("BurstLevelAnalysisDecoder: No observation cache available");
            return std::nullopt;
        }
        
        FieldBurstLevelStats stats;
        stats.field_id = field_id;
        stats.has_data = false;
        
        // Get rendered field representation from cache
        auto field_opt = obs_cache_->get_field(node_id, field_id);
        if (!field_opt) {
            ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Field {} not available for rendering", field_id.value());
            return stats;
        }
        
        auto field = field_opt.value();
        auto descriptor = field->get_descriptor(field_id);
        if (!descriptor) {
            ORC_LOG_WARN("BurstLevelAnalysisDecoder: No descriptor for field {}", field_id.value());
            return stats;
        }
        
        // Color burst is in the blanking interval, typically:
        // NTSC: lines ~10-15 (after vertical blank)
        // PAL: lines ~15-20
        // Sample from early blanking lines where burst is strongest
        
        std::vector<double> burst_amplitudes;
        
        // Scan lines typically containing burst (9-17 for NTSC, 6-20 for PAL)
        int burst_search_start = 6;
        int burst_search_end = std::min(20, (int)descriptor->height / 10);
        
        // Get a chunk of samples from burst region to measure amplitude
        for (int line = burst_search_start; line < burst_search_end; ++line) {
            auto line_data = field->get_line(field_id, line);
            if (!line_data) continue;
            
            // For composite video, we measure the envelope around the burst
            // Sample from the middle of the line (after sync and blanking porch)
            size_t sample_start = descriptor->width / 8;  // Skip sync region
            size_t sample_end = std::min(sample_start + descriptor->width / 4, descriptor->width);
            
            if (sample_start < sample_end) {
                std::vector<uint16_t> samples(line_data + sample_start, line_data + sample_end);
                double median_amplitude = calculate_median_amplitude(samples);
                burst_amplitudes.push_back(median_amplitude);
            }
        }
        
        // Calculate median burst level
        if (!burst_amplitudes.empty()) {
            double total_amplitude = 0.0;
            for (double amp : burst_amplitudes) {
                total_amplitude += amp;
            }
            double median_sample = total_amplitude / burst_amplitudes.size();
            
            // Convert to IRE (color burst should be around 20-40 IRE for standard video)
            stats.median_burst_ire = sample_to_ire(static_cast<uint16_t>(median_sample));
            stats.has_data = true;
        }
        
        // Extract frame number if available
        if (descriptor->frame_number) {
            stats.frame_number = descriptor->frame_number.value();
        }
        
        ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Field {} burst level: {:.2f} IRE",
                     field_id.value(), stats.median_burst_ire);
        return stats;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("BurstLevelAnalysisDecoder: Exception getting burst level for field {}: {}",
                     field_id.value(), e.what());
        return std::nullopt;
    }
}

std::vector<FieldBurstLevelStats> BurstLevelAnalysisDecoder::get_burst_level_for_all_fields(
    NodeID node_id,
    size_t max_fields,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    std::vector<FieldBurstLevelStats> results;
    
    try {
        if (!obs_cache_) {
            ORC_LOG_ERROR("BurstLevelAnalysisDecoder: No observation cache available");
            return results;
        }
        
        ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Processing burst level analysis for node '{}'",
                     node_id.to_string());
        
        // Get total field count at this node
        size_t total_fields = obs_cache_->get_field_count(node_id);
        if (total_fields == 0) {
            ORC_LOG_WARN("BurstLevelAnalysisDecoder: No fields available at node '{}'",
                        node_id.to_string());
            return results;
        }
        
        // Limit to max_fields if specified
        if (max_fields > 0 && max_fields < total_fields) {
            total_fields = max_fields;
        }
        
        ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Processing {} fields", total_fields);
        
        // Process each field
        for (size_t i = 0; i < total_fields; ++i) {
            FieldID fid(i);
            auto stats_opt = get_burst_level_for_field(node_id, fid);
            if (stats_opt) {
                results.push_back(stats_opt.value());
            }
            
            // Progress callback
            if (progress_callback) {
                progress_callback(i + 1, total_fields, "Processing field " + std::to_string(i));
            }
        }
        
        ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Processed {} fields", results.size());
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("BurstLevelAnalysisDecoder: Exception processing all fields: {}", e.what());
    }
    
    return results;
}

std::vector<FrameBurstLevelStats> BurstLevelAnalysisDecoder::get_burst_level_by_frames(
    NodeID node_id,
    size_t max_frames,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    std::vector<FrameBurstLevelStats> results;
    
    try {
        ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Processing burst level analysis by frames for node '{}'",
                     node_id.to_string());
        
        // First get all field stats
        auto field_stats = get_burst_level_for_all_fields(node_id, max_frames * 2, nullptr);
        
        if (field_stats.empty()) {
            ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: No field stats available");
            return results;
        }
        
        // Group fields into frames (2 fields per frame)
        std::map<int32_t, std::vector<FieldBurstLevelStats>> frames_map;
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
            
            FrameBurstLevelStats frame_stat;
            frame_stat.frame_number = frame_number;
            frame_stat.median_burst_ire = 0.0;
            frame_stat.has_data = false;
            frame_stat.field_count = 0;
            
            // Accumulate stats from both fields
            double total_burst = 0.0;
            for (const auto& field_stat : fields) {
                if (field_stat.has_data) {
                    total_burst += field_stat.median_burst_ire;
                    frame_stat.has_data = true;
                }
                frame_stat.field_count++;
            }
            
            // Average the burst level
            if (frame_stat.has_data && frame_stat.field_count > 0) {
                frame_stat.median_burst_ire = total_burst / frame_stat.field_count;
            }
            
            results.push_back(frame_stat);
            frame_count++;
            
            // Progress callback
            if (progress_callback) {
                progress_callback(frame_count, frames_map.size(), 
                                "Processing frame " + std::to_string(frame_number));
            }
        }
        
        ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Processed {} frames", results.size());
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("BurstLevelAnalysisDecoder: Exception processing frames: {}", e.what());
    }
    
    return results;
}

} // namespace orc
