/*
 * File:        observer.h
 * Module:      orc-core
 * Purpose:     Observer base class
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <orc/stage/frame_id.h>
#include <orc/stage/observation/observation_context.h>
#include <orc/stage/observation/observation_schema.h>
#include <orc/stage/video_frame_representation.h>

#include <string>
#include <vector>

namespace orc {

// Confidence level for observations
enum class ConfidenceLevel {
  NONE,    // No valid observation
  LOW,     // Low confidence
  MEDIUM,  // Medium confidence
  HIGH     // High confidence
};

/**
 * @brief Convert confidence level to string
 */
inline std::string confidence_level_to_string(ConfidenceLevel level) {
  switch (level) {
    case ConfidenceLevel::NONE:
      return "none";
    case ConfidenceLevel::LOW:
      return "low";
    case ConfidenceLevel::MEDIUM:
      return "medium";
    case ConfidenceLevel::HIGH:
      return "high";
    default:
      return "unknown";
  }
}

/**
 * @brief Base class for observers
 *
 * Observers measure properties of the video signal and populate the
 * ObservationContext with their findings. They are instantiated by
 * stages that need observations (typically sinks, but also transforms
 * that require specific metadata).
 *
 * Observers write to namespaced keys in the ObservationContext to
 * avoid collisions. Each observer declares what observations it provides
 * via get_provided_observations().
 */
class Observer {
 public:
  virtual ~Observer() = default;

  /**
   * @brief Get observer name
   * @return Human-readable observer name (e.g., "BiphaseObserver")
   */
  virtual std::string observer_name() const = 0;

  /**
   * @brief Get observer version
   * @return Version string (e.g., "1.0.0")
   */
  virtual std::string observer_version() const = 0;

  /**
   * @brief Process a single frame and populate observation context
   *
   * Observers iterate both fields within the frame and write observations
   * keyed by derived FieldIDs (frame_id * 2 + field_idx).
   *
   * @param representation Video frame representation (CVBS_U10_4FSC domain)
   * @param frame_id Frame identifier
   * @param context Observation context to populate
   */
  virtual void process_frame(const VideoFrameRepresentation& representation,
                             FrameID frame_id,
                             IObservationContext& context) = 0;

  /**
   * @brief Declare observations this observer provides
   *
   * Returns a list of observation keys that this observer will write
   * to the context. Used for pipeline validation and documentation.
   *
   * @return Vector of observation keys
   */
  virtual std::vector<ObservationKey> get_provided_observations() const = 0;
};

}  // namespace orc
