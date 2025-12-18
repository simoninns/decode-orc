// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "include/passthrough_splitter_stage.h"

namespace orc {

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
