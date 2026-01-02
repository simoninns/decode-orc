/*
 * File:        field_invert_stage.cpp
 * Module:      orc-core
 * Purpose:     Field inversion stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include <field_invert_stage.h>
#include <stage_registry.h>
#include <preview_helpers.h>
#include <logging.h>

namespace orc {

// Register the stage
ORC_REGISTER_STAGE(FieldInvertStage)

// Force linker to include this object file
void force_link_FieldInvertStage() {}

std::vector<ArtifactPtr> FieldInvertStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>&)
{
    if (inputs.empty()) {
        throw DAGExecutionError("FieldInvertStage requires one input");
    }
    
    auto input_artifact = inputs[0];
    if (!input_artifact) {
        throw DAGExecutionError("FieldInvertStage received null input artifact");
    }
    
    // Cast to VideoFieldRepresentation
    auto input_vfr = std::dynamic_pointer_cast<const VideoFieldRepresentation>(input_artifact);
    if (!input_vfr) {
        throw DAGExecutionError("FieldInvertStage input must be VideoFieldRepresentation");
    }
    
    // Process and return
    auto output_vfr = process(input_vfr);
    cached_output_ = output_vfr;  // Cache for preview
    ORC_LOG_DEBUG("FieldInvertStage::execute - Set cached_output_ on instance {} to {}", 
                  static_cast<const void*>(this), static_cast<const void*>(output_vfr.get()));
    
    std::vector<ArtifactPtr> outputs;
    outputs.push_back(std::const_pointer_cast<VideoFieldRepresentation>(output_vfr));
    return outputs;
}

std::shared_ptr<const VideoFieldRepresentation> FieldInvertStage::process(
    std::shared_ptr<const VideoFieldRepresentation> source) const
{
    if (!source) {
        return nullptr;
    }
    
    // Create a wrapper that inverts field parity hints
    return std::make_shared<InvertedFieldRepresentation>(source);
}

std::vector<ParameterDescriptor> FieldInvertStage::get_parameter_descriptors(VideoSystem project_format) const
{
    (void)project_format;  // Unused - field invert works with all formats
    // No parameters for this stage
    return {};
}

std::map<std::string, ParameterValue> FieldInvertStage::get_parameters() const
{
    return {};
}

bool FieldInvertStage::set_parameters(const std::map<std::string, ParameterValue>& /*params*/)
{
    return true;
}

std::vector<PreviewOption> FieldInvertStage::get_preview_options() const
{
    ORC_LOG_DEBUG("FieldInvertStage::get_preview_options - Called on instance {}, cached_output_ = {}", 
                  static_cast<const void*>(this), static_cast<const void*>(cached_output_.get()));
    if (!cached_output_) {
        return {};
    }
    
    return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage FieldInvertStage::render_preview(
    const std::string& option_id,
    uint64_t index,
    PreviewNavigationHint hint) const
{
    if (!cached_output_) {
        PreviewImage result{};
        return result;
    }
    
    return PreviewHelpers::render_standard_preview(cached_output_, option_id, index, hint);
}

} // namespace orc
