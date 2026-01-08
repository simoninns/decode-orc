/*
 * File:        burst_level_analysis_decoder.h
 * Module:      orc-core
 * Purpose:     Burst level analysis data extraction for GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_BURST_LEVEL_ANALYSIS_DECODER_H
#define ORC_CORE_BURST_LEVEL_ANALYSIS_DECODER_H

#include "burst_level_observer.h"
#include "field_id.h"
#include "node_id.h"
#include "lru_cache.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orc {

// Forward declarations
class DAG;
class ObservationCache;
class VideoFieldRepresentation;

/**
 * @brief Burst level statistics for a single field
 */
struct FieldBurstLevelStats {
    FieldID field_id;
    double median_burst_ire = 0.0;        ///< Median burst level in IRE
    std::optional<int32_t> frame_number;  ///< Frame number if available from VBI
    bool has_data = false;                ///< True if burst level data was successfully extracted
};

/**
 * @brief Burst level statistics aggregated for a frame (two fields)
 */
struct FrameBurstLevelStats {
    int32_t frame_number;                 ///< Frame number (1-based)
    double median_burst_ire = 0.0;        ///< Average burst level from both fields (IRE)
    bool has_data = false;                ///< True if at least one field had data
    size_t field_count = 0;               ///< Number of fields with data (for averaging)
};

/**
 * @brief Decoder for extracting burst level analysis data from DAG nodes
 * 
 * This class provides the business logic for burst level analysis, allowing the GUI
 * to remain a thin display layer. It extracts BurstLevelObservation data
 * from rendered fields and formats it for graphing.
 */
class BurstLevelAnalysisDecoder {
public:
    /**
     * @brief Construct a burst level analysis decoder
     * @param dag The DAG to extract burst level data from
     */
    explicit BurstLevelAnalysisDecoder(std::shared_ptr<const DAG> dag);
    
    ~BurstLevelAnalysisDecoder() = default;
    
    /**
     * @brief Get burst level statistics for a specific field at a node
     * 
     * @param node_id The node to query
     * @param field_id The field to get burst level data for
     * @return Burst level statistics, or empty optional if not available
     */
    std::optional<FieldBurstLevelStats> get_burst_level_for_field(
        NodeID node_id,
        FieldID field_id);
    
    /**
     * @brief Get burst level statistics for all fields at a node
     * 
     * This method processes all available fields and returns their burst level stats.
     * 
     * @param node_id The node to query
     * @param max_fields Maximum number of fields to process (0 = all)
     * @param progress_callback Optional callback for progress updates (current, total, message)
     * @return Vector of burst level statistics for each field
     */
    std::vector<FieldBurstLevelStats> get_burst_level_for_all_fields(
        NodeID node_id,
        size_t max_fields = 0,
        std::function<void(size_t, size_t, const std::string&)> progress_callback = nullptr);
    
    /**
     * @brief Get burst level statistics aggregated by frame
     * 
     * This method processes all fields and combines them into frame-based statistics.
     * Useful for frame-mode analysis where two fields are shown together.
     * 
     * @param node_id The node to query
     * @param max_frames Maximum number of frames to process (0 = all)
     * @param progress_callback Optional callback for progress updates (current, total, message)
     * @return Vector of burst level statistics for each frame
     */
    std::vector<FrameBurstLevelStats> get_burst_level_by_frames(
        NodeID node_id,
        size_t max_frames = 0,
        std::function<void(size_t, size_t, const std::string&)> progress_callback = nullptr);
    
    /**
     * @brief Update the DAG reference
     * @param dag New DAG to use for decoding
     */
    void update_dag(std::shared_ptr<const DAG> dag);
    
    /**
     * @brief Set the observation cache to use
     * 
     * Allows sharing a single cache across multiple decoders.
     * 
     * @param cache Shared observation cache
     */
    void set_observation_cache(std::shared_ptr<ObservationCache> cache);

private:
    // Extract burst level stats from a field representation
    std::optional<FieldBurstLevelStats> extract_burst_level_stats(
        std::shared_ptr<const VideoFieldRepresentation> field_repr,
        FieldID field_id);
    
    // Cache key for storing processed results
    struct CacheKey {
        NodeID node_id;
        
        bool operator==(const CacheKey& other) const {
            return node_id == other.node_id;
        }
    };
    
    // Hash function for CacheKey
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& key) const {
            return std::hash<NodeID>{}(key.node_id);
        }
    };
    
    std::shared_ptr<const DAG> dag_;
    std::shared_ptr<ObservationCache> obs_cache_;
    BurstLevelObserver observer_;
    
    // LRU cache for processed field stats (after observer extraction, max 100 entries)
    mutable LRUCache<CacheKey, std::vector<FieldBurstLevelStats>, CacheKeyHash> field_cache_;
    
    // LRU cache for processed frame stats (max 100 entries)
    mutable LRUCache<CacheKey, std::vector<FrameBurstLevelStats>, CacheKeyHash> frame_cache_;
};

} // namespace orc

#endif // ORC_CORE_BURST_LEVEL_ANALYSIS_DECODER_H
