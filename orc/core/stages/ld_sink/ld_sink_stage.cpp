/*
 * File:        ld_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     ld-decode sink Stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "ld_sink_stage.h"
#include "ld_sink_stage_deps_interface.h"
#include "stage_registry.h"
#include "preview_renderer.h"
#include "preview_helpers.h"
#include "logging.h"
#include "../../factories.h"
#include <memory>

namespace orc {

// Register stage with registry
ORC_REGISTER_STAGE_WITH_FACTORIES(LDSinkStage)

// Force linker to include this object file
void force_link_LDSinkStage() {}

NodeTypeInfo LDSinkStage::get_node_type_info() const
{
    return NodeTypeInfo{
        NodeType::SINK,              // type
        "ld_sink",                   // stage_name
        "ld-decode Sink",            // display_name
        "Writes TBC fields and metadata to disk. Trigger to export all fields.",  // description
        1,                           // min_inputs
        1,                           // max_inputs
        0,                           // min_outputs
        0,                           // max_outputs
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> LDSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters [[maybe_unused]],
    ObservationContext& observation_context)
{
    (void)observation_context; // TODO: Use for observations
    // Cache input for preview rendering
    if (!inputs.empty()) {
        cached_input_ = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    }
    
    // Sink stages don't produce outputs during normal execution
    // They are triggered manually to write data
    ORC_LOG_DEBUG("LDSink execute called (cached input for preview)");
    return {};  // No outputs
}

std::vector<ParameterDescriptor> LDSinkStage::get_parameter_descriptors(VideoSystem project_format, SourceType source_type) const
{
    (void)project_format;
    (void)source_type;  // Unused - LD sink works with all formats
    return {
        ParameterDescriptor{
            "output_path",
            "TBC Output Path",
            "Path to output TBC file (metadata will be written to .db)",
            ParameterType::FILE_PATH,
            ParameterConstraints{std::nullopt, std::nullopt, std::string(""), {}, false, std::nullopt},
            ".tbc"  // file_extension_hint
        }
    };
}

std::map<std::string, ParameterValue> LDSinkStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["output_path"] = output_path_;
    return params;
}

bool LDSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    auto it = params.find("output_path");
    if (it != params.end()) {
        if (std::holds_alternative<std::string>(it->second)) {
            output_path_ = std::get<std::string>(it->second);
            ORC_LOG_DEBUG("LDSink: output_path set to '{}'", output_path_);
        } else {
            ORC_LOG_ERROR("LDSink: output_path parameter must be string");
            return false;
        }
    }
    
    return true;
}

bool LDSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context)
{
    ORC_LOG_DEBUG("LDSink: Trigger started");
    trigger_status_ = "Starting export...";
    is_processing_.store(true);
    cancel_requested_.store(false);

    const auto fail_trigger = [this](const std::string& status) {
        trigger_status_ = status;
        is_processing_.store(false);
        return false;
    };
    
    // Validate parameters
    auto it = parameters.find("output_path");
    if (it == parameters.end() || !std::holds_alternative<std::string>(it->second)) {
        ORC_LOG_ERROR("LDSink: No output_path parameter");
        return fail_trigger("Error: No output path specified");
    }
    
    std::string output_path = std::get<std::string>(it->second);
    if (output_path.empty()) {
        ORC_LOG_ERROR("LDSink: output_path is empty");
        return fail_trigger("Error: Output path is empty");
    }
    
    // Validate inputs
    if (inputs.empty()) {
        ORC_LOG_ERROR("LDSink: No input provided");
        return fail_trigger("Error: No input connected");
    }
    
    // Get input representation
    auto representation = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    if (!representation) {
        ORC_LOG_ERROR("LDSink: Input is not VideoFieldRepresentation");
        return fail_trigger("Error: Input is not a video field representation");
    }
    
    // Write TBC and metadata
    ORC_LOG_INFO("LDSink: Writing to '{}'", output_path);
    // Clear previous observations to avoid mixing runs
    observation_context.clear();

    std::shared_ptr<ILDSinkStageDeps> deps = this->factories_->get_instance_stage_factories().CreateInstanceLDSinkStageDeps(progress_callback_, is_processing_, cancel_requested_);
    bool success = deps->write_tbc_and_metadata(representation.get(), output_path, observation_context);

    if (success) {
        auto range = representation->field_range();
        trigger_status_ = "Exported " + std::to_string(range.size()) + " fields to " + output_path;
        ORC_LOG_DEBUG("LDSink: Trigger completed successfully");
    } else {
        trigger_status_ = "Error: Failed to write output files";
        ORC_LOG_ERROR("LDSink: Trigger failed");
    }
    
    is_processing_.store(false);
    return success;
}

std::string LDSinkStage::get_trigger_status() const
{
    return trigger_status_;
}

std::vector<PreviewOption> LDSinkStage::get_preview_options() const
{
    return PreviewHelpers::get_standard_preview_options(cached_input_);
}

PreviewImage LDSinkStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    (void)hint;  // Unused for now
    return PreviewHelpers::render_standard_preview(cached_input_, option_id, index);
}

} // namespace orc
