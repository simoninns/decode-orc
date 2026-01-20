/*
 * File:        observer_config.h
 * Module:      orc-core
 * Purpose:     Observer configuration schema and validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "stage_parameter.h"
#include <string>
#include <vector>
#include <map>

namespace orc {

/**
 * @brief Observer configuration utilities
 * 
 * Provides validation and default value handling for observer configuration.
 * Reuses the existing ParameterDescriptor system from stages.
 */
class ObserverConfiguration {
public:
    /**
     * @brief Validate configuration against schema
     * 
     * @param schema Parameter descriptors defining valid configuration
     * @param config Configuration values to validate
     * @param error_message Output parameter for error description
     * @return True if valid, false otherwise
     */
    static bool validate(
        const std::vector<ParameterDescriptor>& schema,
        const std::map<std::string, ParameterValue>& config,
        std::string& error_message);
    
    /**
     * @brief Apply default values from schema
     * 
     * Returns a configuration map with all default values applied
     * for parameters not present in the input.
     * 
     * @param schema Parameter descriptors with defaults
     * @param config Optional initial configuration values
     * @return Configuration with defaults applied
     */
    static std::map<std::string, ParameterValue> apply_defaults(
        const std::vector<ParameterDescriptor>& schema,
        const std::map<std::string, ParameterValue>& config = {});
    
    /**
     * @brief Check if all required parameters are present
     * 
     * @param schema Parameter descriptors
     * @param config Configuration values
     * @param missing_params Output vector of missing required parameter names
     * @return True if all required parameters present
     */
    static bool check_required_parameters(
        const std::vector<ParameterDescriptor>& schema,
        const std::map<std::string, ParameterValue>& config,
        std::vector<std::string>& missing_params);
};

} // namespace orc
