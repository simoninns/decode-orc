/*
 * File:        dropout_analysis_decoder.cpp
 * Module:      orc-core
 * Purpose:     Dropout analysis data extraction implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dropout_analysis_decoder.h"
#include "../../include/logging.h"
#include "../../include/observation_context.h"
#include "../../include/observation_cache.h"
#include "../../include/video_field_representation.h"
#include <algorithm>
#include <numeric>

namespace orc {

DropoutAnalysisDecoder::DropoutAnalysisDecoder(std::shared_ptr<const DAG> dag)
    : dag_(dag)
{
    if (!dag) {
        throw std::invalid_argument("DropoutAnalysisDecoder: DAG cannot be null");
    }
    ORC_LOG_DEBUG("DropoutAnalysisDecoder: Created");
}

void DropoutAnalysisDecoder::update_dag(std::shared_ptr<const DAG> dag)
{
    if (!dag) {
        throw std::invalid_argument("DropoutAnalysisDecoder: DAG cannot be null");
    }
    dag_ = dag;
    ORC_LOG_DEBUG("DropoutAnalysisDecoder: DAG updated");
}

void DropoutAnalysisDecoder::set_observation_cache(std::shared_ptr<ObservationCache> cache)
{
    if (!cache) {
        throw std::invalid_argument("DropoutAnalysisDecoder: ObservationCache cannot be null");
    }
    obs_cache_ = cache;
    ORC_LOG_DEBUG("DropoutAnalysisDecoder: Observation cache updated");
}

std::optional<FieldDropoutStats> DropoutAnalysisDecoder::get_dropout_for_field(
    NodeID node_id,
    FieldID field_id,
    DropoutAnalysisMode mode)
{
    try {
        if (!obs_cache_) {
            ORC_LOG_WARN("DropoutAnalysisDecoder: No observation cache available");
            return std::nullopt;
        }
        
        FieldDropoutStats stats;
        stats.field_id = field_id;
        stats.has_data = false;
        
        // Get rendered field representation from cache
        auto field_opt = obs_cache_->get_field(node_id, field_id);
        if (!field_opt) {
            ORC_LOG_DEBUG("DropoutAnalysisDecoder: Field {} not available for rendering", field_id.value());
            return stats;
        }
        
        auto field = field_opt.value();
        
        // Extract dropout regions from field's hints (from TBC metadata)
        auto dropouts = field->get_dropout_hints(field_id);
        
        // Count and sum dropout lengths
        stats.dropout_count = dropouts.size();
        stats.total_dropout_length = 0.0;
        
        for (const auto& dropout : dropouts) {
            if (mode == DropoutAnalysisMode::VISIBLE_AREA) {
                // For visible area, only count dropouts in active video region
                auto active_hint = field->get_active_line_hint();
                if (active_hint && static_cast<int32_t>(dropout.line) >= active_hint->first_active_field_line && 
                    static_cast<int32_t>(dropout.line) <= active_hint->last_active_field_line) {
                    stats.total_dropout_length += (dropout.end_sample - dropout.start_sample);
                }
            } else {
                // Full field analysis
                stats.total_dropout_length += (dropout.end_sample - dropout.start_sample);
            }
        }
        
        // Extract frame number if available from VBI hints
        (void)field->get_field_parity_hint(field_id);  // Intentionally unused
        auto descriptor = field->get_descriptor(field_id);
        if (descriptor && descriptor->frame_number) {
            stats.frame_number = descriptor->frame_number.value();
        }
        
        stats.has_data = (stats.dropout_count > 0);
        ORC_LOG_DEBUG("DropoutAnalysisDecoder: Field {} has {} dropout regions, total length {}",
                     field_id.value(), stats.dropout_count, stats.total_dropout_length);
        return stats;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("DropoutAnalysisDecoder: Exception getting dropout for field {}: {}",
                     field_id.value(), e.what());
        return std::nullopt;
    }
}

std::vector<FieldDropoutStats> DropoutAnalysisDecoder::get_dropout_for_all_fields(
    NodeID node_id,
    DropoutAnalysisMode mode,
    size_t max_fields,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    std::vector<FieldDropoutStats> results;
    
    try {
        if (!obs_cache_) {
            ORC_LOG_ERROR("DropoutAnalysisDecoder: No observation cache available");
            return results;
        }
        
        ORC_LOG_DEBUG("DropoutAnalysisDecoder: Processing dropout analysis for node '{}'",
                     node_id.to_string());
        
        // Get total field count at this node
        size_t total_fields = obs_cache_->get_field_count(node_id);
        if (total_fields == 0) {
            ORC_LOG_WARN("DropoutAnalysisDecoder: No fields available at node '{}'",
                        node_id.to_string());
            return results;
        }
        
        // Limit to max_fields if specified
        if (max_fields > 0 && max_fields < total_fields) {
            total_fields = max_fields;
        }
        
        ORC_LOG_DEBUG("DropoutAnalysisDecoder: Processing {} fields", total_fields);
        
        // Process each field
        for (size_t i = 0; i < total_fields; ++i) {
            FieldID fid(i);
            auto stats_opt = get_dropout_for_field(node_id, fid, mode);
            if (stats_opt) {
                results.push_back(stats_opt.value());
            }
            
            // Progress callback
            if (progress_callback) {
                progress_callback(i + 1, total_fields, "Processing field " + std::to_string(i));
            }
        }
        
        ORC_LOG_DEBUG("DropoutAnalysisDecoder: Processed {} fields", results.size());
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("DropoutAnalysisDecoder: Exception processing all fields: {}", e.what());
    }
    
    return results;
}

std::vector<FrameDropoutStats> DropoutAnalysisDecoder::get_dropout_by_frames(
    NodeID node_id,
    DropoutAnalysisMode mode,
    size_t max_frames,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    std::vector<FrameDropoutStats> results;
    
    try {
        ORC_LOG_DEBUG("DropoutAnalysisDecoder: Processing dropout analysis by frames for node '{}'",
                     node_id.to_string());
        
        // First get all field stats
        auto field_stats = get_dropout_for_all_fields(node_id, mode, max_frames * 2, nullptr);
        
        if (field_stats.empty()) {
            ORC_LOG_DEBUG("DropoutAnalysisDecoder: No field stats available");
            return results;
        }
        
        // Group fields into frames (2 fields per frame)
        std::map<int32_t, std::vector<FieldDropoutStats>> frames_map;
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
            
            FrameDropoutStats frame_stat;
            frame_stat.frame_number = frame_number;
            frame_stat.total_dropout_length = 0.0;
            frame_stat.dropout_count = 0;
            frame_stat.has_data = false;
            
            // Accumulate stats from both fields
            for (const auto& field_stat : fields) {
                frame_stat.total_dropout_length += field_stat.total_dropout_length;
                frame_stat.dropout_count += field_stat.dropout_count;
                if (field_stat.has_data) {
                    frame_stat.has_data = true;
                }
            }
            
            results.push_back(frame_stat);
            frame_count++;
            
            // Progress callback
            if (progress_callback) {
                progress_callback(frame_count, frames_map.size(), 
                                "Processing frame " + std::to_string(frame_number));
            }
        }
        
        ORC_LOG_DEBUG("DropoutAnalysisDecoder: Processed {} frames", results.size());
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("DropoutAnalysisDecoder: Exception processing frames: {}", e.what());
    }
    
    return results;
}

} // namespace orc
