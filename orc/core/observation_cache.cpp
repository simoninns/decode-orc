/*
 * File:        observation_cache.cpp
 * Module:      orc-core
 * Purpose:     Universal cache for observer data implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "include/observation_cache.h"
#include "include/dag_field_renderer.h"
#include "include/video_field_representation.h"
#include "logging.h"

namespace orc {

ObservationCache::ObservationCache(std::shared_ptr<const DAG> dag)
    : dag_(dag)
    , cache_(1000)  // Max 1000 cached field representations
    , field_count_cache_(100)  // Max 100 cached field counts
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
    cache_.clear();
    field_count_cache_.clear();
    ORC_LOG_DEBUG("ObservationCache: All cached observations cleared");
}

void ObservationCache::clear_node(NodeID node_id)
{
    // Note: LRUCache doesn't support selective removal by predicate
    // For now, we just clear the entire cache when any node needs clearing
    // This is safe but may be less efficient than selective clearing
    cache_.clear();
    field_count_cache_.clear();
    ORC_LOG_DEBUG("ObservationCache: Cleared all observations (requested for node '{}')", node_id.to_string());
}

bool ObservationCache::is_cached(NodeID node_id, FieldID field_id) const
{
    CacheKey key{node_id, field_id};
    return cache_.contains(key);
}

size_t ObservationCache::get_field_count(NodeID node_id)
{
    // Check cache first
    auto cached_count = field_count_cache_.get(node_id);
    if (cached_count.has_value()) {
        return cached_count.value();
    }
    
    // Render and cache field 0 to get count (don't waste the render!)
    auto field_repr = render_and_cache(node_id, FieldID(0));
    if (!field_repr.has_value()) {
        ORC_LOG_WARN("ObservationCache: Failed to get field count for node '{}'", node_id.to_string());
        return 0;
    }
    
    size_t count = field_repr.value()->field_count();
    field_count_cache_.put(node_id, count);
    
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
        cache_.put(key, result.representation);
        
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
    // Check cache first
    CacheKey key{node_id, field_id};
    auto cached = cache_.get(key);
    if (cached.has_value()) {
        return cached.value();
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

const ObservationContext& ObservationCache::get_observation_context() const
{
    if (!renderer_) {
        static ObservationContext empty_context;
        return empty_context;
    }
    return renderer_->get_observation_context();
}

} // namespace orc
