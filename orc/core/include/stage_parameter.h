/*
 * File:        stage_parameter.h
 * Module:      orc-core
 * Purpose:     Stage Parameter
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

// =============================================================================
// MVP Architecture Enforcement
// =============================================================================
// This header is part of the CORE internal implementation.
// GUI/CLI code should use parameter_types.h from orc/common instead.
// =============================================================================
#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/include/stage_parameter.h. Use parameter_types.h from orc/common instead."
#endif
#if defined(ORC_CLI_BUILD)
#error "CLI code cannot include core/include/stage_parameter.h. Use parameter_types.h from orc/common instead."
#endif

#include <common_types.h>  // For VideoSystem and SourceType enums
#include <parameter_types.h>  // For parameter type definitions
#include <string>
#include <map>

namespace orc {

// SourceType now defined in common_types.h
// Parameter types now defined in parameter_types.h

/// Interface for stages that expose configurable parameters
class ParameterizedStage {
public:
    virtual ~ParameterizedStage() = default;
    
    /// Get list of parameters this stage supports
    /// @param project_format Optional video format from project context for filtering options
    /// @param source_type Optional source type (Composite/YC) from project context for filtering options
    virtual std::vector<ParameterDescriptor> get_parameter_descriptors(
        VideoSystem project_format = VideoSystem::Unknown,
        SourceType source_type = SourceType::Unknown) const = 0;
    
    /// Get current parameter values
    virtual std::map<std::string, ParameterValue> get_parameters() const = 0;
    
    /// Set parameter values
    /// Returns true if all parameters were valid and set successfully
    virtual bool set_parameters(const std::map<std::string, ParameterValue>& params) = 0;
};

} // namespace orc
