/*
 * File:        passthrough_splitter_stage.cpp
 * Module:      orc-core
 * Purpose:     Passthrough splitter stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include <passthrough_splitter_stage.h>
#include <stage_registry.h>

namespace orc {

// Register the stage
static StageRegistration splitter_registration([]() {
    return std::make_shared<PassthroughSplitterStage>();
});

std::vector<ArtifactPtr> PassthroughSplitterStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>&)
{
    if (inputs.empty()) {
        throw DAGExecutionError("PassthroughSplitterStage requires one input");
    }
    
    // Return input duplicated 3 times
    return {inputs[0], inputs[0], inputs[0]};
}

std::vector<std::shared_ptr<const VideoFieldRepresentation>> 
PassthroughSplitterStage::process(std::shared_ptr<const VideoFieldRepresentation> source) const
{
    // Return multiple copies of the same input
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> outputs;
    for (size_t i = 0; i < output_count(); ++i) {
        outputs.push_back(source);
    }
    return outputs;
}

std::vector<ParameterDescriptor> PassthroughSplitterStage::get_parameter_descriptors() const
{
    // No parameters for this test stage
    return {};
}

std::map<std::string, ParameterValue> PassthroughSplitterStage::get_parameters() const
{
    return {};
}

bool PassthroughSplitterStage::set_parameters(const std::map<std::string, ParameterValue>& /*params*/)
{
    return true;
}

} // namespace orc
