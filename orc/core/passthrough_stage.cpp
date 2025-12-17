/******************************************************************************
 * passthrough_stage.cpp
 *
 * Passthrough (dummy) stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "passthrough_stage.h"

namespace orc {

std::shared_ptr<const VideoFieldRepresentation> PassthroughStage::process(
    std::shared_ptr<const VideoFieldRepresentation> source) const
{
    // Simply return the input unchanged
    return source;
}
std::vector<ParameterDescriptor> PassthroughStage::get_parameter_descriptors() const
{
    // Passthrough stage has no configurable parameters
    return {};
}

std::map<std::string, ParameterValue> PassthroughStage::get_parameters() const
{
    // No parameters
    return {};
}

bool PassthroughStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Accept empty parameter set, reject any attempts to set parameters
    return params.empty();
}
} // namespace orc
