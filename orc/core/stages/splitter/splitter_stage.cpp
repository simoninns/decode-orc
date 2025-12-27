/*
 * File:        splitter_stage.cpp
 * Module:      orc-core
 * Purpose:     Splitter stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include <splitter_stage.h>
#include <stage_registry.h>

namespace orc {

// Register the stage
static StageRegistration splitter_registration([]() {
    return std::make_shared<SplitterStage>();
});

SplitterStage::SplitterStage() : num_outputs_(2) {
}

std::vector<ArtifactPtr> SplitterStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>&)
{
    if (inputs.empty()) {
        throw DAGExecutionError("SplitterStage requires one input");
    }
    
    // Return input duplicated N times
    std::vector<ArtifactPtr> outputs;
    for (size_t i = 0; i < num_outputs_; ++i) {
        outputs.push_back(inputs[0]);
    }
    return outputs;
}

std::vector<std::shared_ptr<const VideoFieldRepresentation>> 
SplitterStage::process(std::shared_ptr<const VideoFieldRepresentation> source) const
{
    // Return multiple copies of the same input
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> outputs;
    for (size_t i = 0; i < output_count(); ++i) {
        outputs.push_back(source);
    }
    return outputs;
}

std::vector<ParameterDescriptor> SplitterStage::get_parameter_descriptors(VideoSystem project_format) const
{
    (void)project_format;  // Unused - splitter works with all formats
    return {
        ParameterDescriptor{
            "num_outputs",
            "Number of Outputs",
            "Number of output copies (2-8)",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(2)},  // min value
                ParameterValue{int32_t(8)},  // max value
                ParameterValue{int32_t(2)},  // default value
                {},  // no allowed strings
                false  // not required
            }
        }
    };
}

std::map<std::string, ParameterValue> SplitterStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["num_outputs"] = static_cast<int32_t>(num_outputs_);
    return params;
}

bool SplitterStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    auto it = params.find("num_outputs");
    if (it != params.end()) {
        if (std::holds_alternative<int32_t>(it->second)) {
            int32_t value = std::get<int32_t>(it->second);
            if (value >= 2 && value <= 8) {
                num_outputs_ = static_cast<size_t>(value);
                return true;
            }
        }
    }
    return true;  // Silently ignore invalid parameters
}

} // namespace orc
