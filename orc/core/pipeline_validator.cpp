/*
 * File:        pipeline_validator.cpp
 * Module:      orc-core
 * Purpose:     Pipeline validation implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "include/pipeline_validator.h"
#include <sstream>
#include <algorithm>

namespace orc {

PipelineValidator::ValidationResult 
PipelineValidator::validate_observation_dependencies(
    const std::vector<DAGStagePtr>& stages)
{
    ValidationResult result;
    
    // Track accumulated provided observations as we walk the pipeline
    std::set<ObservationKey> available_observations;
    
    // Walk through stages in execution order
    for (size_t i = 0; i < stages.size(); ++i) {
        const auto& stage = stages[i];
        if (!stage) {
            result.valid = false;
            result.errors.push_back("Null stage at position " + std::to_string(i));
            continue;
        }
        
        // Get stage's required observations
        auto required = collect_required_observations(stage);
        
        // Check if all required observations are available
        for (const auto& obs_key : required) {
            if (available_observations.find(obs_key) == available_observations.end()) {
                // Required observation not available
                std::ostringstream oss;
                oss << "Stage '" << stage->get_node_type_info().name 
                    << "' at position " << i
                    << " requires observation '" << obs_key.full_key()
                    << "' which is not provided by any earlier stage";
                result.errors.push_back(oss.str());
                result.valid = false;
            }
        }
        
        // Add this stage's provided observations to available set
        auto provided = collect_provided_observations(stage);
        for (const auto& obs_key : provided) {
            // Check for duplicates (warn only)
            if (available_observations.find(obs_key) != available_observations.end()) {
                std::ostringstream oss;
                oss << "Stage '" << stage->get_node_type_info().name
                    << "' provides observation '" << obs_key.full_key()
                    << "' which is already provided by an earlier stage (will override)";
                result.warnings.push_back(oss.str());
            }
            available_observations.insert(obs_key);
        }
    }
    
    return result;
}

std::set<ObservationKey> PipelineValidator::collect_required_observations(
    const DAGStagePtr& stage)
{
    std::set<ObservationKey> required;
    auto obs_vec = stage->get_required_observations();
    required.insert(obs_vec.begin(), obs_vec.end());
    return required;
}

std::set<ObservationKey> PipelineValidator::collect_provided_observations(
    const DAGStagePtr& stage)
{
    std::set<ObservationKey> provided;
    auto obs_vec = stage->get_provided_observations();
    provided.insert(obs_vec.begin(), obs_vec.end());
    return provided;
}

} // namespace orc
