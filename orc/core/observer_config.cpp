/*
 * File:        observer_config.cpp
 * Module:      orc-core
 * Purpose:     Observer configuration validation implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "include/observer_config.h"
#include <sstream>

namespace orc {

bool ObserverConfiguration::validate(
    const std::vector<ParameterDescriptor>& schema,
    const std::map<std::string, ParameterValue>& config,
    std::string& error_message)
{
    // Check for required parameters
    std::vector<std::string> missing_params;
    if (!check_required_parameters(schema, config, missing_params)) {
        std::ostringstream oss;
        oss << "Missing required parameters: ";
        for (size_t i = 0; i < missing_params.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << missing_params[i];
        }
        error_message = oss.str();
        return false;
    }
    
    // Check for unknown parameters
    for (const auto& [key, value] : config) {
        bool found = false;
        for (const auto& desc : schema) {
            if (desc.name == key) {
                found = true;
                break;
            }
        }
        if (!found) {
            error_message = "Unknown parameter: " + key;
            return false;
        }
    }
    
    // Type validation would go here
    // For now, assume ParameterValue variant handles type safety
    
    error_message.clear();
    return true;
}

std::map<std::string, ParameterValue> ObserverConfiguration::apply_defaults(
    const std::vector<ParameterDescriptor>& schema,
    const std::map<std::string, ParameterValue>& config)
{
    std::map<std::string, ParameterValue> result = config;
    
    for (const auto& desc : schema) {
        // If parameter not present and has default, add it
        if (result.find(desc.name) == result.end() && desc.constraints.default_value.has_value()) {
            result[desc.name] = desc.constraints.default_value.value();
        }
    }
    
    return result;
}

bool ObserverConfiguration::check_required_parameters(
    const std::vector<ParameterDescriptor>& schema,
    const std::map<std::string, ParameterValue>& config,
    std::vector<std::string>& missing_params)
{
    missing_params.clear();
    
    for (const auto& desc : schema) {
        // Required if constraints.required is true or no default value
        if (desc.constraints.required || !desc.constraints.default_value.has_value()) {
            if (config.find(desc.name) == config.end()) {
                missing_params.push_back(desc.name);
            }
        }
    }
    
    return missing_params.empty();
}

} // namespace orc
