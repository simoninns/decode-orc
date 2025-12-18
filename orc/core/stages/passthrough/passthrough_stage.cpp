/******************************************************************************
 * passthrough_stage.cpp
 *
 * Passthrough (dummy) stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "passthrough_stage.h"
#include "stage_registry.h"

namespace orc {

// Register the stage
static StageRegistration passthrough_registration([]() {
    return std::make_shared<PassthroughStage>();
});

std::vector<ArtifactPtr> PassthroughStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, std::string>&)
{
    if (inputs.empty()) {
        throw DAGExecutionError("PassthroughStage requires one input");
    }
    
    // Return input unchanged
    return {inputs[0]};
}

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
