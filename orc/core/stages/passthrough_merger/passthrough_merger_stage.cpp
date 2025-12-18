// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include <passthrough_merger_stage.h>
#include <stage_registry.h>

namespace orc {

// Register the stage
static StageRegistration merger_registration([]() {
    return std::make_shared<PassthroughMergerStage>();
});

std::vector<ArtifactPtr> PassthroughMergerStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, std::string>&)
{
    if (inputs.empty()) {
        throw DAGExecutionError("PassthroughMergerStage requires at least one input");
    }
    
    // Return first input
    return {inputs[0]};
}

std::shared_ptr<const VideoFieldRepresentation> 
PassthroughMergerStage::process(
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const
{
    // Simply return the first input
    if (sources.empty()) {
        return nullptr;
    }
    return sources[0];
}

std::vector<ParameterDescriptor> PassthroughMergerStage::get_parameter_descriptors() const
{
    // No parameters for this test stage
    return {};
}

std::map<std::string, ParameterValue> PassthroughMergerStage::get_parameters() const
{
    return {};
}

bool PassthroughMergerStage::set_parameters(const std::map<std::string, ParameterValue>& /*params*/)
{
    return true;
}

} // namespace orc
