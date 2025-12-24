/*
 * File:        passthrough_stage.cpp
 * Module:      orc-core
 * Purpose:     Passthrough processing stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "passthrough_stage.h"
#include "preview_renderer.h"
#include "preview_helpers.h"
#include "logging.h"
#include "stage_registry.h"

namespace orc {

// Register the stage
static StageRegistration passthrough_registration([]() {
    return std::make_shared<PassthroughStage>();
});

std::vector<ArtifactPtr> PassthroughStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>&)
{
    if (inputs.empty()) {
        throw DAGExecutionError("PassthroughStage requires one input");
    }
    
    // Cache the output for preview
    cached_output_ = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    
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

std::vector<PreviewOption> PassthroughStage::get_preview_options() const
{
    return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage PassthroughStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    return PreviewHelpers::render_standard_preview(cached_output_, option_id, index);
}

} // namespace orc
