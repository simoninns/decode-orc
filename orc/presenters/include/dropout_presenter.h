/*
 * File:        dropout_presenter.h
 * Module:      orc-presenters
 * Purpose:     Dropout editing presenter - MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <node_id.h>
#include <field_id.h>
#include <orc_rendering.h>  // For DropoutRegion

// Forward declare core Project type
namespace orc {
    class Project;
    class VideoFieldRepresentation;
}

// Forward declare presenters
namespace orc::presenters {
    class ProjectPresenter;
}

namespace orc::presenters {

// Forward declarations
class Project;

// Use public API DropoutRegion
using DropoutRegion = orc::public_api::DropoutRegion;

/**
 * @brief Per-field dropout map view model
 */
struct FieldDropoutMap {
    FieldID field_id;
    std::vector<DropoutRegion> additions;    ///< Dropouts to add
    std::vector<DropoutRegion> removals;     ///< Dropouts to remove
    
    FieldDropoutMap() : field_id(0) {}
    explicit FieldDropoutMap(FieldID id) : field_id(id) {}
};

/**
 * @brief Dropout detection data
 */
struct DetectedDropout {
    int line;               ///< Line number
    int pixel_start;        ///< Start pixel
    int pixel_end;          ///< End pixel
    double severity;        ///< Severity score (0-1)
    bool is_confirmed;      ///< Whether dropout is confirmed
};

/**
 * @brief Dropout decision type
 */
enum class DropoutDecision {
    Conceal,        ///< Conceal the dropout (apply correction)
    Ignore,         ///< Ignore the dropout (no correction)
    FlagOnly        ///< Flag but don't correct
};

/**
 * @brief Dropout correction applied to a field
 */
struct DropoutCorrection {
    FieldID field_id;               ///< Field this applies to
    int line;                       ///< Line number
    int pixel_start;                ///< Start pixel
    int pixel_end;                  ///< End pixel
    DropoutDecision decision;       ///< User decision
    std::string correction_method;  ///< Method used for concealment
};

/**
 * @brief Statistics about dropouts in a field
 */
struct FieldDropoutStats {
    FieldID field_id;
    int total_detected;         ///< Total dropouts detected
    int confirmed_count;        ///< Number confirmed by user
    int concealed_count;        ///< Number being concealed
    int ignored_count;          ///< Number being ignored
    double coverage_percentage; ///< Percentage of field affected
};

/**
 * @brief DropoutPresenter - Manages dropout detection and correction
 * 
 * This presenter extracts dropout editing logic from the GUI layer.
 * It provides a clean interface for:
 * - Detecting dropouts in fields
 * - Managing user decisions about dropouts
 * - Applying correction methods
 * - Tracking dropout statistics
 * 
 * The presenter coordinates between detection algorithms and
 * the correction pipeline.
 */
class DropoutPresenter {
public:
    /**
     * @brief Construct presenter for a project
     * @param project_presenter ProjectPresenter to delegate project operations to
     */
    explicit DropoutPresenter(orc::presenters::ProjectPresenter& project_presenter);
    
    /**
     * @brief Destructor
     */
    ~DropoutPresenter();
    
    // Disable copy, enable move
    DropoutPresenter(const DropoutPresenter&) = delete;
    DropoutPresenter& operator=(const DropoutPresenter&) = delete;
    DropoutPresenter(DropoutPresenter&&) noexcept;
    DropoutPresenter& operator=(DropoutPresenter&&) noexcept;
    
    // === Detection ===
    
    /**
     * @brief Detect dropouts in a field
     * @param node_id Node to detect from
     * @param field_id Field to analyze
     * @return List of detected dropouts
     */
    std::vector<DetectedDropout> detectDropouts(NodeID node_id, FieldID field_id);
    
    /**
     * @brief Get cached dropout detections
     * @param node_id Node to query
     * @param field_id Field to query
     * @return List of detected dropouts (empty if not cached)
     */
    std::vector<DetectedDropout> getDetectedDropouts(NodeID node_id, FieldID field_id) const;
    
    /**
     * @brief Clear detection cache for a field
     */
    void clearDetections(NodeID node_id, FieldID field_id);
    
    // === Decision Management ===
    
    /**
     * @brief Update dropout decision
     * @param node_id Node the dropout belongs to
     * @param field_id Field the dropout is in
     * @param line Line number
     * @param pixel_start Start pixel
     * @param decision User decision
     * @param correction_method Method to use (if concealing)
     */
    void updateDropoutDecision(NodeID node_id, FieldID field_id, 
                              int line, int pixel_start,
                              DropoutDecision decision,
                              const std::string& correction_method = "");
    
    /**
     * @brief Get all corrections for a field
     * @param node_id Node to query
     * @param field_id Field to query
     * @return List of corrections
     */
    std::vector<DropoutCorrection> getCorrections(NodeID node_id, FieldID field_id) const;
    
    /**
     * @brief Remove a correction
     * @param node_id Node the correction belongs to
     * @param field_id Field the correction is in
     * @param line Line number
     * @param pixel_start Start pixel
     */
    void removeCorrection(NodeID node_id, FieldID field_id, int line, int pixel_start);
    
    /**
     * @brief Clear all corrections for a field
     */
    void clearCorrections(NodeID node_id, FieldID field_id);
    
    // === Statistics ===
    
    /**
     * @brief Get dropout statistics for a field
     * @param node_id Node to query
     * @param field_id Field to query
     * @return Statistics
     */
    FieldDropoutStats getFieldStats(NodeID node_id, FieldID field_id) const;
    
    /**
     * @brief Get overall dropout statistics for a node
     * @param node_id Node to query
     * @return Map of field ID to statistics
     */
    std::map<FieldID, FieldDropoutStats> getAllStats(NodeID node_id) const;
    
    // === Batch Operations ===
    
    /**
     * @brief Apply a decision to all similar dropouts in a field
     * @param node_id Node to operate on
     * @param field_id Field to operate on
     * @param reference_dropout Reference dropout to match
     * @param decision Decision to apply
     * @return Number of dropouts affected
     */
    int applyDecisionToSimilar(NodeID node_id, FieldID field_id,
                              const DetectedDropout& reference_dropout,
                              DropoutDecision decision);
    
    /**
     * @brief Auto-decide all dropouts in a field based on severity
     * @param node_id Node to operate on
     * @param field_id Field to operate on
     * @param severity_threshold Threshold for auto-concealment
     * @return Number of dropouts processed
     */
    int autoDecideDropouts(NodeID node_id, FieldID field_id, double severity_threshold);
    
    // === Export/Import ===
    
    /**
     * @brief Export corrections to file
     * @param node_id Node to export from
     * @param file_path Output file path
     * @return true on success
     */
    bool exportCorrections(NodeID node_id, const std::string& file_path) const;
    
    /**
     * @brief Import corrections from file
     * @param node_id Node to import to
     * @param file_path Input file path
     * @return true on success
     */
    bool importCorrections(NodeID node_id, const std::string& file_path);
    
    // === Field Access (for dropout editor) ===
    
    /**
     * @brief Get field data for display from VideoFieldRepresentation
     * @param field_repr Field representation from DAG execution
     * @param field_id Field to retrieve
     * @param width Output field width
     * @param height Output field height
     * @return Grayscale field data (8-bit), or empty if not available
     */
    std::vector<uint8_t> getFieldData(const std::shared_ptr<const orc::VideoFieldRepresentation>& field_repr,
                                      FieldID field_id, int& width, int& height);
    
    /**
     * @brief Get source dropout regions from VideoFieldRepresentation
     * @param field_repr Field representation from DAG execution
     * @param field_id Field to retrieve dropouts for
     * @return List of detected dropout regions
     */
    std::vector<DropoutRegion> getSourceDropouts(const std::shared_ptr<const orc::VideoFieldRepresentation>& field_repr,
                                                  FieldID field_id);
    
    /**
     * @brief Get total number of fields from VideoFieldRepresentation
     * @param field_repr Field representation from DAG execution
     * @return Number of fields
     */
    size_t getFieldCount(const std::shared_ptr<const orc::VideoFieldRepresentation>& field_repr);
    
    /**
     * @brief Get dropout map for a node (if it's a dropout_map stage)
     * @param node_id Dropout map stage node
     * @return Map of field IDs to dropout modifications
     */
    std::map<uint64_t, FieldDropoutMap> getDropoutMap(NodeID node_id);
    
    /**
     * @brief Set dropout map for a node (if it's a dropout_map stage)
     * @param node_id Dropout map stage node
     * @param dropout_map New dropout map
     * @return true on success
     */
    bool setDropoutMap(NodeID node_id, const std::map<uint64_t, FieldDropoutMap>& dropout_map);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orc::presenters
