/*
 * File:        snr_analysis_decoder.h
 * Module:      orc-core
 * Purpose:     SNR analysis data extraction for GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_SNR_ANALYSIS_DECODER_H
#define ORC_CORE_SNR_ANALYSIS_DECODER_H

#include "snr_analysis_observer.h"
#include "field_id.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace orc {

// Forward declarations
class DAG;
class ObservationCache;
class VideoFieldRepresentation;

/**
 * @brief SNR statistics for a single field
 */
struct FieldSNRStats {
    FieldID field_id;
    double white_snr = 0.0;               ///< White SNR value (dB)
    double black_psnr = 0.0;              ///< Black PSNR value (dB)
    bool has_white_snr = false;           ///< True if white SNR data is available
    bool has_black_psnr = false;          ///< True if black PSNR data is available
    std::optional<int32_t> frame_number;  ///< Frame number if available from VBI
    bool has_data = false;                ///< True if any SNR data was successfully extracted
};

/**
 * @brief SNR statistics aggregated for a frame (two fields)
 */
struct FrameSNRStats {
    int32_t frame_number;                 ///< Frame number (1-based)
    double white_snr = 0.0;               ///< Average white SNR (dB)
    double black_psnr = 0.0;              ///< Average black PSNR (dB)
    bool has_white_snr = false;           ///< True if white SNR data is available
    bool has_black_psnr = false;          ///< True if black PSNR data is available
    bool has_data = false;                ///< True if at least one field had data
    size_t field_count = 0;               ///< Number of fields with data (for averaging)
};

/**
 * @brief Decoder for extracting SNR analysis data from DAG nodes
 * 
 * This class provides the business logic for SNR analysis, allowing the GUI
 * to remain a thin display layer. It extracts SNRAnalysisObservation data
 * from rendered fields and formats it for graphing.
 */
class SNRAnalysisDecoder {
public:
    /**
     * @brief Construct an SNR analysis decoder
     * @param dag The DAG to extract SNR data from
     */
    explicit SNRAnalysisDecoder(std::shared_ptr<const DAG> dag);
    
    ~SNRAnalysisDecoder() = default;
    
    /**
     * @brief Get SNR statistics for a specific field at a node
     * 
     * @param node_id The node to query
     * @param field_id The field to get SNR data for
     * @param mode Analysis mode (white, black, or both)
     * @return SNR statistics, or empty optional if not available
     */
    std::optional<FieldSNRStats> get_snr_for_field(
        const std::string& node_id,
        FieldID field_id,
        SNRAnalysisMode mode);
    
    /**
     * @brief Get SNR statistics for all fields at a node
     * 
     * This method processes all available fields and returns their SNR stats.
     * 
     * @param node_id The node to query
     * @param mode Analysis mode (white, black, or both)
     * @param max_fields Maximum number of fields to process (0 = all)
     * @return Vector of SNR statistics for each field
     */
    std::vector<FieldSNRStats> get_snr_for_all_fields(
        const std::string& node_id,
        SNRAnalysisMode mode,
        size_t max_fields = 0);
    
    /**
     * @brief Get SNR statistics aggregated by frame
     * 
     * This method processes all fields and combines them into frame-based statistics.
     * Useful for frame-mode analysis where two fields are shown together.
     * 
     * @param node_id The node to query
     * @param mode Analysis mode (white, black, or both)
     * @param max_frames Maximum number of frames to process (0 = all)
     * @return Vector of SNR statistics for each frame
     */
    std::vector<FrameSNRStats> get_snr_by_frames(
        const std::string& node_id,
        SNRAnalysisMode mode,
        size_t max_frames = 0);
    
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
    // Extract SNR stats from a field representation
    std::optional<FieldSNRStats> extract_snr_stats(
        std::shared_ptr<const VideoFieldRepresentation> field_repr,
        FieldID field_id,
        SNRAnalysisMode mode);
    
    // Cache key for storing processed results
    struct CacheKey {
        std::string node_id;
        SNRAnalysisMode mode;
        
        bool operator==(const CacheKey& other) const {
            return node_id == other.node_id && mode == other.mode;
        }
    };
    
    // Hash function for CacheKey
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& key) const {
            std::size_t h1 = std::hash<std::string>{}(key.node_id);
            std::size_t h2 = std::hash<int>{}(static_cast<int>(key.mode));
            return h1 ^ (h2 << 1);
        }
    };
    
    std::shared_ptr<ObservationCache> obs_cache_;
    SNRAnalysisObserver observer_;
    
    // Cache for processed field stats (after observer extraction)
    std::unordered_map<CacheKey, std::vector<FieldSNRStats>, CacheKeyHash> field_cache_;
    
    // Cache for processed frame stats
    std::unordered_map<CacheKey, std::vector<FrameSNRStats>, CacheKeyHash> frame_cache_;
};

} // namespace orc

#endif // ORC_CORE_SNR_ANALYSIS_DECODER_H
