/*
 * File:        dropout_presenter.h
 * Module:      orc-presenters
 * Purpose:     Dropout editing presenter - MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <frame_id.h>
#include <node_id.h>
#include <orc_rendering.h>  // For DropoutRegion

#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward declare core Project type
namespace orc {
class Project;
}  // namespace orc

// Forward declare presenters
namespace orc::presenters {
class IProjectPresenter;
}

namespace orc::presenters {

// Use public API DropoutRegion
using DropoutRegion = orc::DropoutRegion;

/**
 * @brief Per-frame dropout map view model
 *
 * Coordinates are frame-flat 0-based (line = frame-flat line index,
 * start_sample/end_sample = sample within that line).
 */
struct FrameDropoutMap {
  FrameID frame_id;
  std::vector<DropoutRegion> additions;  ///< Dropouts to add
  std::vector<DropoutRegion> removals;   ///< Dropouts to remove

  FrameDropoutMap() : frame_id(0) {}
  explicit FrameDropoutMap(FrameID id) : frame_id(id) {}
};

/**
 * @brief Dropout detection data
 */
struct DetectedDropout {
  int line;           ///< Line number
  int pixel_start;    ///< Start pixel
  int pixel_end;      ///< End pixel
  double severity;    ///< Severity score (0-1)
  bool is_confirmed;  ///< Whether dropout is confirmed
};

/**
 * @brief Dropout decision type
 */
enum class DropoutDecision {
  Conceal,  ///< Conceal the dropout (apply correction)
  Ignore,   ///< Ignore the dropout (no correction)
  FlagOnly  ///< Flag but don't correct
};

/**
 * @brief Dropout correction applied to a frame
 */
struct DropoutCorrection {
  FrameID frame_id;               ///< Frame this applies to
  int line;                       ///< Line number
  int pixel_start;                ///< Start pixel
  int pixel_end;                  ///< End pixel
  DropoutDecision decision;       ///< User decision
  std::string correction_method;  ///< Method used for concealment
};

/**
 * @brief Statistics about dropouts in a frame
 */
struct FrameDropoutStats {
  FrameID frame_id;
  int total_detected;          ///< Total dropouts detected
  int confirmed_count;         ///< Number confirmed by user
  int concealed_count;         ///< Number being concealed
  int ignored_count;           ///< Number being ignored
  double coverage_percentage;  ///< Percentage of frame affected
};

/**
 * @brief DropoutPresenter - Manages dropout detection and correction
 *
 * This presenter extracts dropout editing logic from the GUI layer.
 * It provides a clean interface for:
 * - Detecting dropouts in frames
 * - Managing user decisions about dropouts
 * - Applying correction methods
 * - Tracking dropout statistics
 *
 * All coordinates are frame-flat 0-based (line = frame-flat line index,
 * start_sample/end_sample = sample within that line), matching the
 * DropoutMapStage serialisation format.
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
  explicit DropoutPresenter(
      orc::presenters::IProjectPresenter& project_presenter);

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
   * @brief Detect dropouts in a frame
   * @param node_id Node to detect from
   * @param frame_id Frame to analyze
   * @return List of detected dropouts
   */
  std::vector<DetectedDropout> detectDropouts(NodeID node_id, FrameID frame_id);

  /**
   * @brief Get cached dropout detections
   * @param node_id Node to query
   * @param frame_id Frame to query
   * @return List of detected dropouts (empty if not cached)
   */
  std::vector<DetectedDropout> getDetectedDropouts(NodeID node_id,
                                                   FrameID frame_id) const;

  /**
   * @brief Clear detection cache for a frame
   */
  void clearDetections(NodeID node_id, FrameID frame_id);

  // === Decision Management ===

  /**
   * @brief Update dropout decision
   * @param node_id Node the dropout belongs to
   * @param frame_id Frame the dropout is in
   * @param line Frame-flat line number
   * @param pixel_start Start pixel
   * @param decision User decision
   * @param correction_method Method to use (if concealing)
   */
  void updateDropoutDecision(NodeID node_id, FrameID frame_id, int line,
                             int pixel_start, DropoutDecision decision,
                             const std::string& correction_method = "");

  /**
   * @brief Get all corrections for a frame
   * @param node_id Node to query
   * @param frame_id Frame to query
   * @return List of corrections
   */
  std::vector<DropoutCorrection> getCorrections(NodeID node_id,
                                                FrameID frame_id) const;

  /**
   * @brief Remove a correction
   * @param node_id Node the correction belongs to
   * @param frame_id Frame the correction is in
   * @param line Frame-flat line number
   * @param pixel_start Start pixel
   */
  void removeCorrection(NodeID node_id, FrameID frame_id, int line,
                        int pixel_start);

  /**
   * @brief Clear all corrections for a frame
   */
  void clearCorrections(NodeID node_id, FrameID frame_id);

  // === Statistics ===

  /**
   * @brief Get dropout statistics for a frame
   * @param node_id Node to query
   * @param frame_id Frame to query
   * @return Statistics
   */
  FrameDropoutStats getFrameStats(NodeID node_id, FrameID frame_id) const;

  /**
   * @brief Get overall dropout statistics for a node
   * @param node_id Node to query
   * @return Map of frame ID to statistics
   */
  std::map<FrameID, FrameDropoutStats> getAllStats(NodeID node_id) const;

  // === Batch Operations ===

  /**
   * @brief Apply a decision to all similar dropouts in a frame
   * @param node_id Node to operate on
   * @param frame_id Frame to operate on
   * @param reference_dropout Reference dropout to match
   * @param decision Decision to apply
   * @return Number of dropouts affected
   */
  int applyDecisionToSimilar(NodeID node_id, FrameID frame_id,
                             const DetectedDropout& reference_dropout,
                             DropoutDecision decision);

  /**
   * @brief Auto-decide all dropouts in a frame based on severity
   * @param node_id Node to operate on
   * @param frame_id Frame to operate on
   * @param severity_threshold Threshold for auto-concealment
   * @return Number of dropouts processed
   */
  int autoDecideDropouts(NodeID node_id, FrameID frame_id,
                         double severity_threshold);

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

  // === Frame Access (for dropout editor) ===

  /**
   * @brief Get frame data for display from artifact
   *
   * Returns grayscale pixel data for the full frame (all lines). The returned
   * image has dimensions (width × height) where width =
   * samples_per_line_nominal and height = frame_height. Coordinates in the
   * returned buffer are frame-flat 0-based (line 0 at top).
   *
   * @param vfr_handle Opaque handle to VideoFrameRepresentation
   * @param frame_id Frame to retrieve
   * @param width Output frame width (samples_per_line_nominal)
   * @param height Output frame height (total lines)
   * @return Grayscale frame data (8-bit), or empty if not available
   */
  std::vector<uint8_t> getFrameData(const std::shared_ptr<void>& vfr_handle,
                                    FrameID frame_id, int& width, int& height);

  /**
   * @brief Get source dropout regions for a frame from artifact
   *
   * Returns dropout regions in frame-flat coordinates (line = frame-flat
   * 0-based line index, start_sample/end_sample = sample within that line).
   * These match the DropoutEntrySpec coordinate system used by DropoutMapStage.
   *
   * @param vfr_handle Opaque handle to VideoFrameRepresentation
   * @param frame_id Frame to retrieve dropouts for
   * @return List of dropout regions in frame-flat coordinates
   */
  std::vector<DropoutRegion> getSourceDropouts(
      const std::shared_ptr<void>& vfr_handle, FrameID frame_id);

  /**
   * @brief Get total number of frames from artifact
   * @param vfr_handle Opaque handle to VideoFrameRepresentation
   * @return Number of frames
   */
  size_t getFrameCount(const std::shared_ptr<void>& vfr_handle);

  /**
   * @brief Get dropout map for a node (if it's a dropout_map stage)
   * @param node_id Dropout map stage node
   * @return Map of frame IDs to dropout modifications
   */
  std::map<uint64_t, FrameDropoutMap> getDropoutMap(NodeID node_id);

  /**
   * @brief Set dropout map for a node (if it's a dropout_map stage)
   * @param node_id Dropout map stage node
   * @param dropout_map New dropout map (keyed by frame_id)
   * @return true on success
   */
  bool setDropoutMap(NodeID node_id,
                     const std::map<uint64_t, FrameDropoutMap>& dropout_map);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace orc::presenters
