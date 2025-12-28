/*
 * File:        observation_cache.h
 * Module:      orc-core
 * Purpose:     Universal cache for observer data across video source
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_OBSERVATION_CACHE_H
#define ORC_CORE_OBSERVATION_CACHE_H

#include "field_id.h"
#include "node_id.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <optional>
#include <mutex>

namespace orc {

// Forward declarations
class DAG;
class VideoFieldRepresentation;
class DAGFieldRenderer;

/**
 * @brief Universal cache for rendered fields across the entire video source
 * 
 * This class provides centralized caching of rendered VideoFieldRepresentation
 * for all fields at any node in the DAG. All analysis dialogs (VBI, dropout, etc.)
 * can share this cache, avoiding redundant field rendering.
 * 
 * The cache stores the full VideoFieldRepresentation for each field, allowing any
 * observer to be run on-demand without re-rendering.
 */
class ObservationCache {
public:
    /**
     * @brief Construct an observation cache
     * @param dag The DAG to extract observations from
     */
    explicit ObservationCache(std::shared_ptr<const DAG> dag);
    
    ~ObservationCache() = default;
    
    /**
     * @brief Get rendered field representation for a specific field at a node
     * 
     * If not cached, renders the field and caches it.
     * 
     * @param node_id The node to query
     * @param field_id The field to get
     * @return Field representation, or nullopt if field cannot be rendered
     */
    std::optional<std::shared_ptr<const VideoFieldRepresentation>> get_field(
        NodeID node_id,
        FieldID field_id);
    
    /**
     * @brief Pre-populate cache for all fields at a node
     * 
     * Renders all fields at the specified node and caches their observations.
     * This is more efficient than individual field queries due to DAG caching.
     * 
     * @param node_id The node to pre-populate
     * @param max_fields Maximum fields to cache (0 = all)
     * @return Number of fields successfully cached
     */
    size_t populate_node(NodeID node_id, size_t max_fields = 0);
    
    /**
     * @brief Check if observations are cached for a specific field
     * 
     * @param node_id The node to check
     * @param field_id The field to check
     * @return True if cached
     */
    bool is_cached(NodeID node_id, FieldID field_id) const;
    
    /**
     * @brief Get the total field count at a node
     * 
     * Renders field 0 if necessary to determine count.
     * 
     * @param node_id The node to query
     * @return Field count, or 0 if node cannot be rendered
     */
    size_t get_field_count(NodeID node_id);
    
    /**
     * @brief Update the DAG reference and clear cache
     * @param dag New DAG to use
     */
    void update_dag(std::shared_ptr<const DAG> dag);
    
    /**
     * @brief Clear all cached observations
     */
    void clear();
    
    /**
     * @brief Clear cached observations for a specific node
     * @param node_id The node to clear
     */
    void clear_node(NodeID node_id);

private:
    // Cache key for storing observations
    struct CacheKey {
        NodeID node_id;
        FieldID field_id;
        
        bool operator==(const CacheKey& other) const {
            return node_id == other.node_id && field_id == other.field_id;
        }
    };
    
    // Hash function for CacheKey
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& key) const {
            std::size_t h1 = std::hash<NodeID>{}(key.node_id);
            std::size_t h2 = std::hash<int32_t>{}(key.field_id.value());
            return h1 ^ (h2 << 1);
        }
    };
    
    std::shared_ptr<const DAG> dag_;
    std::shared_ptr<DAGFieldRenderer> renderer_;
    
    // Cache of rendered field representations
    std::unordered_map<CacheKey, std::shared_ptr<const VideoFieldRepresentation>, CacheKeyHash> cache_;
    
    // Cache of field counts per node
    std::unordered_map<NodeID, size_t> field_count_cache_;
    
    // Mutex for thread-safe access
    mutable std::mutex cache_mutex_;
    
    // Render a single field and cache it
    std::optional<std::shared_ptr<const VideoFieldRepresentation>> render_and_cache(
        NodeID node_id,
        FieldID field_id);
};

} // namespace orc

#endif // ORC_CORE_OBSERVATION_CACHE_H
