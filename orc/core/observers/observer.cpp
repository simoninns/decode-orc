/*
 * File:        observer.cpp
 * Module:      orc-core
 * Purpose:     Observer base class implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "observer.h"
#include "observer_config.h"
#include <stdexcept>

namespace orc {

void Observer::set_configuration(const std::map<std::string, ParameterValue>& config)
{
    // Get schema
    auto schema = get_configuration_schema();
    
    // Validate configuration
    std::string error_message;
    if (!ObserverConfiguration::validate(schema, config, error_message)) {
        throw std::invalid_argument("Invalid observer configuration: " + error_message);
    }
    
    // Apply defaults and store
    configuration_ = ObserverConfiguration::apply_defaults(schema, config);
}

} // namespace orc
