/*
 * File:        field_quality_observer.h
 * Module:      decode-orc Plugin SDK (stage contract)
 * Purpose:     Field quality observer for field quality metrics
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

// SDK TIER: stage/observation — stage contract type crossing the plugin
// boundary. A layout change here bumps the host ABI version.

#include <orc/stage/observation/observer.h>

#include <array>
#include <cstdint>

namespace orc {

// Forward declarations
class ObservationContext;

/**
 * @brief Observer for field quality analysis
 *
 * Calculates quality metrics for each field based on:
 * - Dropout count/density
 * - Phase correctness
 * - Signal-to-noise estimates (if available)
 *
 * Used by disc mapping policy to choose best duplicate when multiple
 * fields have the same VBI frame number.
 */
class FieldQualityObserver : public Observer {
 public:
  FieldQualityObserver() = default;

  std::string observer_name() const override { return "FieldQualityObserver"; }

  std::string observer_version() const override { return "1.0.0"; }

  void process_frame(const VideoFrameRepresentation& representation,
                     FrameID frame_id, IObservationContext& context) override;

  std::vector<ObservationKey> get_provided_observations() const override {
    return {
        {"disc_quality", "quality_score", ObservationType::DOUBLE,
         "Field quality score 0.0-1.0"},
        {"disc_quality", "dropout_count", ObservationType::INT32,
         "Number of dropouts detected"},
        {"disc_quality", "phase_valid", ObservationType::BOOL,
         "Phase correctness indicator"},
    };
  }

 private:
  /**
   * @brief Calculate quality score from field data
   *
   * Combines multiple quality indicators:
   * - Dropout density
   * - Phase correctness
   * - Signal metrics
   *
   * @return Quality score 0.0-1.0
   */
  double calculate_quality_score(const VideoFrameRepresentation& representation,
                                 FrameID frame_id, size_t field_height,
                                 const SourceParameters& vp) const;
};

}  // namespace orc
