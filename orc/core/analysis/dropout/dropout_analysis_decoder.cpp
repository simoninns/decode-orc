/*
 * File:        dropout_analysis_decoder.cpp
 * Module:      orc-core
 * Purpose:     Dropout analysis data extraction implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dropout_analysis_decoder.h"
#include "../../include/observation_cache.h"
#include "../../include/video_field_representation.h"
#include "../../observers/observation_history.h"
#include "../../observers/biphase_observer.h"
#include "../../include/logging.h"
#include <algorithm>
#include <thread>
#include <atomic>

namespace orc {

DropoutAnalysisDecoder::DropoutAnalysisDecoder(std::shared_ptr<const DAG> dag)
    : observer_(DropoutAnalysisMode::FULL_FIELD)
{
    if (!dag) {
        throw std::invalid_argument("DropoutAnalysisDecoder: DAG cannot be null");
    }
    
    obs_cache_ = std::make_shared<ObservationCache>(dag);
}

void DropoutAnalysisDecoder::update_dag(std::shared_ptr<const DAG> dag)
{
    if (!dag) {
        throw std::invalid_argument("DropoutAnalysisDecoder: DAG cannot be null");
    }
    
    if (obs_cache_) {
        obs_cache_->update_dag(dag);
    } else {
        obs_cache_ = std::make_shared<ObservationCache>(dag);
    }
    
    // Clear processed caches when DAG changes
    field_cache_.clear();
    frame_cache_.clear();
    ORC_LOG_DEBUG("DropoutAnalysisDecoder: DAG updated, caches cleared");
}

void DropoutAnalysisDecoder::set_observation_cache(std::shared_ptr<ObservationCache> cache)
{
    if (!cache) {
        throw std::invalid_argument("DropoutAnalysisDecoder: ObservationCache cannot be null");
    }
    
    obs_cache_ = cache;
    
    // Clear processed caches when cache changes
    field_cache_.clear();
    frame_cache_.clear();
    ORC_LOG_DEBUG("DropoutAnalysisDecoder: Observation cache updated");
}

std::optional<FieldDropoutStats> DropoutAnalysisDecoder::get_dropout_for_field(
    NodeID node_id,
    FieldID field_id,
    DropoutAnalysisMode mode)
{
    try {
        // Set the observer mode
        observer_.set_mode(mode);
        
        // Get field representation from cache (will render if not cached)
        auto field_repr = obs_cache_->get_field(node_id, field_id);
        
        if (!field_repr.has_value()) {
            ORC_LOG_WARN("DropoutAnalysisDecoder: Failed to get field {} at node '{}'",
                        field_id.value(), node_id.to_string());
            return std::nullopt;
        }
        
        return extract_dropout_stats(field_repr.value(), field_id, mode);
        
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
    // Check processed cache first (only if max_fields is 0, meaning all fields)
    if (max_fields == 0) {
        CacheKey key{node_id, mode};
        auto it = field_cache_.find(key);
        if (it != field_cache_.end()) {
            ORC_LOG_DEBUG("DropoutAnalysisDecoder: Returning cached field data for node '{}'", node_id.to_string());
            return it->second;
        }
    }
    
    std::vector<FieldDropoutStats> results;
    
    try {
        // Get field count from observation cache
        size_t field_count = obs_cache_->get_field_count(node_id);
        
        if (field_count == 0) {
            ORC_LOG_WARN("DropoutAnalysisDecoder: No fields available for node '{}'", node_id.to_string());
            return results;
        }
        
        if (max_fields > 0 && max_fields < field_count) {
            field_count = max_fields;
        }
        
        results.resize(field_count);
        
        ORC_LOG_INFO("DropoutAnalysisDecoder: Processing {} fields at node '{}' with {} threads",
                    field_count, node_id.to_string(), std::thread::hardware_concurrency());
        
        // Process fields in parallel using all available cores
        const size_t num_threads = std::max(1u, std::thread::hardware_concurrency());
        const size_t chunk_size = (field_count + num_threads - 1) / num_threads;
        
        std::vector<std::thread> threads;
        std::atomic<size_t> progress_counter{0};
        
        auto process_chunk = [&](size_t start_idx, size_t end_idx) {
            for (size_t i = start_idx; i < end_idx; ++i) {
                FieldID field_id(i);
                auto stats = get_dropout_for_field(node_id, field_id, mode);
                
                if (stats.has_value()) {
                    results[i] = stats.value();
                } else {
                    // Add empty entry to maintain field index consistency
                    FieldDropoutStats empty_stats;
                    empty_stats.field_id = field_id;
                    empty_stats.has_data = false;
                    results[i] = empty_stats;
                }
                
                // Report progress periodically via callback
                size_t current = progress_counter.fetch_add(1) + 1;
                if (progress_callback && current % 100 == 0) {
                    progress_callback(current, field_count, "Processing dropout analysis...");
                }
            }
        };
        
        // Launch threads
        for (size_t t = 0; t < num_threads; ++t) {
            size_t start_idx = t * chunk_size;
            size_t end_idx = std::min(start_idx + chunk_size, field_count);
            
            if (start_idx < field_count) {
                threads.emplace_back(process_chunk, start_idx, end_idx);
            }
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Report final progress
        if (progress_callback) {
            progress_callback(field_count, field_count, "Dropout analysis complete");
        }
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("DropoutAnalysisDecoder: Exception processing all fields: {}", e.what());
    }
    
    // Cache the results if we processed all fields
    if (max_fields == 0 && !results.empty()) {
        CacheKey key{node_id, mode};
        field_cache_[key] = results;
        ORC_LOG_DEBUG("DropoutAnalysisDecoder: Cached field data for node '{}' ({} fields)", 
                     node_id.to_string(), results.size());
    }
    
    return results;
}

std::vector<FrameDropoutStats> DropoutAnalysisDecoder::get_dropout_by_frames(
    NodeID node_id,
    DropoutAnalysisMode mode,
    size_t max_frames,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    // Check cache first (only if max_frames is 0, meaning all frames)
    if (max_frames == 0) {
        CacheKey key{node_id, mode};
        auto it = frame_cache_.find(key);
        if (it != frame_cache_.end()) {
            ORC_LOG_DEBUG("DropoutAnalysisDecoder: Returning cached frame data for node '{}'", node_id.to_string());
            return it->second;
        }
    }
    
    std::vector<FrameDropoutStats> results;
    
    try {
        // Get all field stats first
        auto field_stats = get_dropout_for_all_fields(node_id, mode, max_frames * 2, progress_callback);
        
        // Combine pairs of fields into frames
        size_t frame_count = field_stats.size() / 2;
        if (max_frames > 0 && max_frames < frame_count) {
            frame_count = max_frames;
        }
        
        results.reserve(frame_count);
        
        for (size_t frame_idx = 0; frame_idx < frame_count; ++frame_idx) {
            FrameDropoutStats frame_stats;
            frame_stats.frame_number = frame_idx + 1;  // 1-based frame numbers
            
            size_t field1_idx = frame_idx * 2;
            size_t field2_idx = frame_idx * 2 + 1;
            
            if (field1_idx < field_stats.size()) {
                const auto& f1 = field_stats[field1_idx];
                if (f1.has_data) {
                    frame_stats.total_dropout_length += f1.total_dropout_length;
                    frame_stats.dropout_count += f1.dropout_count;
                    frame_stats.has_data = true;
                }
            }
            
            if (field2_idx < field_stats.size()) {
                const auto& f2 = field_stats[field2_idx];
                if (f2.has_data) {
                    frame_stats.total_dropout_length += f2.total_dropout_length;
                    frame_stats.dropout_count += f2.dropout_count;
                    frame_stats.has_data = true;
                }
            }
            
            results.push_back(frame_stats);
        }
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("DropoutAnalysisDecoder: Exception processing frames: {}", e.what());
    }
    
    // Cache the results if we processed all frames
    if (max_frames == 0 && !results.empty()) {
        CacheKey key{node_id, mode};
        frame_cache_[key] = results;
        ORC_LOG_DEBUG("DropoutAnalysisDecoder: Cached frame data for node '{}' ({} frames)", 
                     node_id.to_string(), results.size());
    }
    
    return results;
}

std::optional<FieldDropoutStats> DropoutAnalysisDecoder::extract_dropout_stats(
    std::shared_ptr<const VideoFieldRepresentation> field_repr,
    FieldID field_id,
    DropoutAnalysisMode mode)
{
    try {
        // Get the existing observations from the representation
        auto existing_obs = field_repr->get_observations(field_id);
        
        // Create an observation history from existing observations
        ObservationHistory history;
        history.add_observations(field_id, existing_obs);
        
        // Run the dropout observer on this field
        auto observations = observer_.process_field(*field_repr, field_id, history);
        
        // Find the DropoutAnalysisObservation we just created
        for (const auto& obs : observations) {
            if (obs->observation_type() == "DropoutAnalysis") {
                auto dropout_obs = std::dynamic_pointer_cast<DropoutAnalysisObservation>(obs);
                if (dropout_obs && dropout_obs->mode == mode) {
                    FieldDropoutStats stats;
                    stats.field_id = field_id;
                    stats.total_dropout_length = dropout_obs->total_dropout_length;
                    stats.dropout_count = dropout_obs->dropout_count;
                    stats.frame_number = dropout_obs->frame_number;
                    stats.has_data = true;
                    
                    ORC_LOG_DEBUG("DropoutAnalysisDecoder: Field {} extracted: count={}, length={:.1f}",
                                 field_id.value(), stats.dropout_count, stats.total_dropout_length);
                    
                    return stats;
                }
            }
        }
        
        ORC_LOG_DEBUG("DropoutAnalysisDecoder: No dropout observation found for field {}",
                     field_id.value());
        return std::nullopt;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("DropoutAnalysisDecoder: Exception extracting dropout stats: {}", e.what());
        return std::nullopt;
    }
}

} // namespace orc
