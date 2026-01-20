/*
 * File:        pipeline_validator.h
 * Module:      orc-core
 * Purpose:     Pipeline validation for observation dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "../stages/stage.h"
#include "observation_schema.h"
#include <vector>
#include <string>
#include <set>

namespace orc {

/**
 * @brief Pipeline validation utilities
 * 
 * Validates that observation dependencies are satisfied before
 * pipeline execution begins. This catches configuration errors early
 * with helpful error messages.
 */
class PipelineValidator {
public:
    /**
     * @brief Validation result
     */
    struct ValidationResult {
        bool valid;                          // True if validation passed
        std::vector<std::string> errors;     // Error messages (empty if valid)
        std::vector<std::string> warnings;   // Warning messages
        
        ValidationResult() : valid(true) {}
    };
    
    /**
     * @brief Validate observation dependencies for a pipeline
     * 
     * Walks through stages in execution order and ensures that:
     * 1. Each stage's required observations are provided by earlier stages
     * 2. No circular dependencies exist
     * 3. All observation types match
     * 
     * @param stages Pipeline stages in execution order
     * @return Validation result
     */
    static ValidationResult validate_observation_dependencies(
        const std::vector<DAGStagePtr>& stages);
    
private:
    /**
     * @brief Collect all required observations from a stage
     */
    static std::set<ObservationKey> collect_required_observations(
        const DAGStagePtr& stage);
    
    /**
     * @brief Collect all provided observations from a stage
     */
    static std::set<ObservationKey> collect_provided_observations(
        const DAGStagePtr& stage);
};

} // namespace orc
