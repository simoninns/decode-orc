/*
* File:        triggerable_stage.h
 * Module:      orc-core/stages
 * Purpose:     Triggerable interface for stages that can be manually executed
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "../include/artifact.h"
#include "../include/stage_parameter.h"
#include "../include/observation_context_interface.h"
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <variant>

namespace orc
{

/**
 * @brief Progress callback for triggerable stages
 *
 * @param current Current progress value (e.g., frames processed)
 * @param total Total work to be done (e.g., total frames)
 * @param message Status message describing current operation
 */
using TriggerProgressCallback = std::function<void(size_t current, size_t total, const std::string& message)>;

/**
 * @brief Triggerable interface for stages that can be manually executed
 *
 * Stages that implement this interface can be triggered from the GUI,
 * causing them to process their entire input range and perform their action.
 */
class TriggerableStage {
public:
  virtual ~TriggerableStage() = default;

  /**
   * @brief Trigger the stage to process its input
   *
   * For sinks, this means reading all fields from input and writing to output file.
   *
   * @param inputs Input artifacts (typically one VideoFieldRepresentation)
   * @param parameters Stage parameters
   * @param observation_context The observation context interface
   * @return True if trigger succeeded, false otherwise
   */
  virtual bool trigger(
      const std::vector<ArtifactPtr>& inputs,
      const std::map<std::string, ParameterValue>& parameters,
      IObservationContext& observation_context
  ) = 0;

  /**
   * @brief Get status message after trigger
   * @return Status message describing what was done
   */
  virtual std::string get_trigger_status() const = 0;

  /**
   * @brief Set progress callback for long-running trigger operations
   *
   * @param callback Function to call with progress updates (current, total, message)
   */
  virtual void set_progress_callback(TriggerProgressCallback callback) {
   (void)callback; // Default implementation does nothing
  }

  /**
   * @brief Check if trigger is currently in progress
   * @return True if trigger is running, false otherwise
   */
  virtual bool is_trigger_in_progress() const {
   return false; // Default implementation assumes no async operation
  }

  /**
   * @brief Cancel an in-progress trigger operation
   *
   * Only relevant for stages that support async trigger operations.
   * Default implementation does nothing.
   */
  virtual void cancel_trigger() {
   // Default implementation does nothing
  }
};

}