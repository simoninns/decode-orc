/*
 * File:        passthrough_complex_stage.cpp
 * Module:      orc-core
 * Purpose:     Complex passthrough stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include <passthrough_complex_stage.h>
#include <stage_registry.h>

namespace orc {

// Register the stage
static StageRegistration complex_registration([]() {
    return std::make_shared<PassthroughComplexStage>();
});

std::vector<ArtifactPtr> PassthroughComplexStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>&)
{
    if (inputs.size() < 3) {
        throw DAGExecutionError("PassthroughComplexStage requires 3 inputs");
    }
    
    // Return first two inputs
    return {inputs[0], inputs[1]};
}

std::vector<std::shared_ptr<const VideoFieldRepresentation>> 
PassthroughComplexStage::process(
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const
{
    // Return each input as a separate output (identity mapping)
    return sources;
}

std::vector<ParameterDescriptor> PassthroughComplexStage::get_parameter_descriptors() const
{
    // No parameters for this test stage
    return {};
}

std::map<std::string, ParameterValue> PassthroughComplexStage::get_parameters() const
{
    return {};
}

bool PassthroughComplexStage::set_parameters(const std::map<std::string, ParameterValue>& /*params*/)
{
    return true;
}

} // namespace orc
