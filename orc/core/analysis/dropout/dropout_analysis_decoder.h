/*
 * File:        dropout_analysis_decoder.h
 * Module:      orc-core
 * Purpose:     Dropout analysis data extraction for GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_DROPOUT_ANALYSIS_DECODER_H
#define ORC_CORE_DROPOUT_ANALYSIS_DECODER_H

#include "field_id.h"
#include "node_id.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orc {

// Forward declarations
class DAG;
class ObservationCache;
class VideoFieldRepresentation;

// Temporary stub types until dropout analysis is ported to the new observation API
enum class DropoutAnalysisMode { FULL_FIELD, VISIBLE_AREA };

/**
 * @brief Dropout statistics for a single field
 */
struct FieldDropoutStats {
    FieldID field_id;
    double total_dropout_length = 0.0;   ///< Total dropout length in samples
    size_t dropout_count = 0;             ///< Number of dropout regions
    std::optional<int32_t> frame_number;  ///< Frame number if available from VBI
    bool has_data = false;                ///< True if dropout data was successfully extracted
};

/**
 * @brief Dropout statistics aggregated for a frame (two fields)
 */
struct FrameDropoutStats {
    int32_t frame_number;                 ///< Frame number (1-based)
    double total_dropout_length = 0.0;    ///< Combined dropout length from both fields
    size_t dropout_count = 0;              ///< Combined dropout count from both fields
    bool has_data = false;                 ///< True if at least one field had data
};

/**
 * @brief Decoder for extracting dropout analysis data from DAG nodes
 * 
 * This class provides the business logic for dropout analysis, allowing the GUI
 * to remain a thin display layer. It extracts DropoutAnalysisObservation data
 * from rendered fields and formats it for graphing.
 */
class DropoutAnalysisDecoder {
public:
    /**
     * @brief Construct a dropout analysis decoder
     * @param dag The DAG to extract dropout data from
     */
    explicit DropoutAnalysisDecoder(std::shared_ptr<const DAG> dag);
    
    ~DropoutAnalysisDecoder() = default;
    
    /**
     * @brief Get dropout statistics for a specific field at a node
     * 
     * @param node_id The node to query
     * @param field_id The field to get dropout data for
     * @param mode Analysis mode (full field or visible area only)
     * @return Dropout statistics, or empty optional if not available
     */
    std::optional<FieldDropoutStats> get_dropout_for_field(
        NodeID node_id,
        FieldID field_id,
        DropoutAnalysisMode mode);
    
    /**
     * @brief Get dropout statistics for all fields at a node
     * 
     * This method processes all available fields and returns their dropout stats.
     * 
     * @param node_id The node to query
     * @param mode Analysis mode (full field or visible area only)
     * @param max_fields Maximum number of fields to process (0 = all)
     * @param progress_callback Optional callback for progress updates (current, total, message)
     * @return Vector of dropout statistics for each field
     */
    std::vector<FieldDropoutStats> get_dropout_for_all_fields(
        NodeID node_id,
        DropoutAnalysisMode mode,
        size_t max_fields = 0,
        std::function<void(size_t, size_t, const std::string&)> progress_callback = nullptr);
    
    /**
     * @brief Get dropout statistics aggregated by frame
     * 
     * This method processes all fields and combines them into frame-based statistics.
     * Useful for frame-mode analysis where two fields are shown together.
     * 
     * @param node_id The node to query
     * @param mode Analysis mode (full field or visible area only)
     * @param max_frames Maximum number of frames to process (0 = all)
     * @param progress_callback Optional callback for progress updates (current, total, message)
     * @return Vector of dropout statistics for each frame
     */
    std::vector<FrameDropoutStats> get_dropout_by_frames(
        NodeID node_id,
        DropoutAnalysisMode mode,
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
    std::shared_ptr<const DAG> dag_;
    std::shared_ptr<ObservationCache> obs_cache_;
};

} // namespace orc

#endif // ORC_CORE_DROPOUT_ANALYSIS_DECODER_H
