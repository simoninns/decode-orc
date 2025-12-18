// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "include/passthrough_merger_stage.h"

namespace orc {

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
