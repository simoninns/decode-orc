/*
 * File:        observation_cache.cpp
 * Module:      orc-core
 * Purpose:     Universal cache for observer data implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "include/observation_cache.h"
#include "include/dag_field_renderer.h"
#include "include/video_field_representation.h"
#include "logging.h"

namespace orc {

ObservationCache::ObservationCache(std::shared_ptr<const DAG> dag)
    : dag_(dag)
{
    if (!dag_) {
        throw std::invalid_argument("ObservationCache: DAG cannot be null");
    }
    
    renderer_ = std::make_shared<DAGFieldRenderer>(dag_);
}

void ObservationCache::update_dag(std::shared_ptr<const DAG> dag)
{
    if (!dag) {
        throw std::invalid_argument("ObservationCache: DAG cannot be null");
    }
    
    dag_ = dag;
    renderer_ = std::make_shared<DAGFieldRenderer>(dag_);
    clear();
}

void ObservationCache::clear()
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();
    field_count_cache_.clear();
    ORC_LOG_DEBUG("ObservationCache: All cached observations cleared");
}

void ObservationCache::clear_node(NodeID node_id)
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    size_t cleared = 0;
    auto it = cache_.begin();
    while (it != cache_.end()) {
        if (it->first.node_id == node_id) {
            it = cache_.erase(it);
            ++cleared;
        } else {
            ++it;
        }
    }
    
    field_count_cache_.erase(node_id);
    
    if (cleared > 0) {
        ORC_LOG_DEBUG("ObservationCache: Cleared {} observations for node '{}'", cleared, node_id.to_string());
    }
}

bool ObservationCache::is_cached(NodeID node_id, FieldID field_id) const
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    CacheKey key{node_id, field_id};
    return cache_.find(key) != cache_.end();
}

size_t ObservationCache::get_field_count(NodeID node_id)
{
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        // Check cache first
        auto it = field_count_cache_.find(node_id);
        if (it != field_count_cache_.end()) {
            return it->second;
        }
    }
    
    // Render and cache field 0 to get count (don't waste the render!)
    auto field_repr = render_and_cache(node_id, FieldID(0));
    if (!field_repr.has_value()) {
        ORC_LOG_WARN("ObservationCache: Failed to get field count for node '{}'", node_id.to_string());
        return 0;
    }
    
    size_t count = field_repr.value()->field_count();
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        field_count_cache_[node_id] = count;
    }
    
    return count;
}

std::optional<std::shared_ptr<const VideoFieldRepresentation>> ObservationCache::render_and_cache(
    NodeID node_id,
    FieldID field_id)
{
    try {
        // Render the field
        auto result = renderer_->render_field_at_node(node_id, field_id);
        
        if (!result.is_valid || !result.representation) {
            ORC_LOG_WARN("ObservationCache: Failed to render field {} at node '{}': {}",
                        field_id.value(), node_id.to_string(), result.error_message);
            return std::nullopt;
        }
        
        // Cache the full representation
        CacheKey key{node_id, field_id};
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            cache_[key] = result.representation;
        }
        
        return result.representation;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("ObservationCache: Exception rendering field {} at node '{}': {}",
                     field_id.value(), node_id.to_string(), e.what());
        return std::nullopt;
    }
}

std::optional<std::shared_ptr<const VideoFieldRepresentation>> ObservationCache::get_field(
    NodeID node_id,
    FieldID field_id)
{
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        // Check cache first
        CacheKey key{node_id, field_id};
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }
    }
    
    // Not cached - render and cache
    return render_and_cache(node_id, field_id);
}

size_t ObservationCache::populate_node(NodeID node_id, size_t max_fields)
{
    size_t field_count = get_field_count(node_id);
    if (field_count == 0) {
        return 0;
    }
    
    if (max_fields > 0 && max_fields < field_count) {
        field_count = max_fields;
    }
    
    ORC_LOG_DEBUG("ObservationCache: Populating {} fields for node '{}'", field_count, node_id.to_string());
    
    size_t cached_count = 0;
    for (size_t i = 0; i < field_count; ++i) {
        FieldID field_id(i);
        
        // Skip if already cached
        if (is_cached(node_id, field_id)) {
            ++cached_count;
            continue;
        }
        
        // Render and cache
        if (render_and_cache(node_id, field_id).has_value()) {
            ++cached_count;
        }
    }
    
    ORC_LOG_DEBUG("ObservationCache: Cached {} / {} fields for node '{}'", 
                cached_count, field_count, node_id.to_string());
    
    return cached_count;
}

} // namespace orc
