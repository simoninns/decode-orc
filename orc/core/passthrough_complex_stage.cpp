// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "include/passthrough_complex_stage.h"

namespace orc {

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
