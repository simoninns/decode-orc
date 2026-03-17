/*
 * File:        daphne_vbi_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Generate .VBI binary files
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#include "daphne_vbi_sink_stage.h"
#include "daphne_vbi_writer_util.h"
#include "stage_registry.h"
#include "preview_renderer.h"
#include "buffered_file_io.h"
#include "logging.h"
#include <filesystem>
#include <memory>
#include "../../factories.h"

namespace orc
{

// Register stage with registry
ORC_REGISTER_STAGE_WITH_FACTORIES(DaphneVBISinkStage)

// Force linker to include this object file
void force_link_DaphneVBISinkStage() {}

NodeTypeInfo DaphneVBISinkStage::get_node_type_info() const
{
    return NodeTypeInfo{
        NodeType::SINK,              // type
        "daphne_vbi_sink",                   // stage_name
        "Daphne VBI Sink",            // display_name
        "Generates Daphne VBI file ( https://www.daphne-emu.com:9443/mediawiki/index.php/VBIInfo )",  // description
        1,                           // min_inputs
        1,                           // max_inputs
        0,                           // min_outputs
        0,                           // max_outputs
        VideoFormatCompatibility::ALL,
        SinkCategory::THIRD_PARTY
    };
}

std::vector<ArtifactPtr> DaphneVBISinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters [[maybe_unused]],
    ObservationContext& observation_context)
{
    (void)inputs;
    (void)observation_context;

    return {};  // No outputs
}

std::vector<ParameterDescriptor> DaphneVBISinkStage::get_parameter_descriptors(VideoSystem project_format, SourceType source_type) const
{
    (void)project_format;
    (void)source_type;
    return {
        ParameterDescriptor{
            "output_path",
            "VBI Output Path",
            "Path to output VBI file",
            ParameterType::FILE_PATH,
            ParameterConstraints{std::nullopt, std::nullopt, std::string(""), {}, false, std::nullopt},
            ".vbi"  // file_extension_hint
        }
    };
}

std::map<std::string, ParameterValue> DaphneVBISinkStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["output_path"] = output_path_;
    return params;
}

bool DaphneVBISinkStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    auto it = params.find("output_path");
    if (it != params.end()) {
        if (std::holds_alternative<std::string>(it->second)) {
            output_path_ = std::get<std::string>(it->second);
            ORC_LOG_DEBUG("DaphneVBISink: output_path set to '{}'", output_path_);
        } else {
            ORC_LOG_ERROR("DaphneVBISink: output_path parameter must be string");
            return false;
        }
    }

    return true;
}

bool DaphneVBISinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context)
{
    ORC_LOG_DEBUG("DaphneVBISink: Trigger started");
    trigger_status_ = "Starting export...";
    is_processing_.store(true);
    cancel_requested_.store(false);

    // Validate parameters
    auto it = parameters.find("output_path");
    if (it == parameters.end() || !std::holds_alternative<std::string>(it->second)) {
        trigger_status_ = "Error: No output path specified";
        ORC_LOG_ERROR("DaphneVBISink: No output_path parameter");
        return false;
    }

    std::string output_path = std::get<std::string>(it->second);
    if (output_path.empty()) {
        trigger_status_ = "Error: Output path is empty";
        ORC_LOG_ERROR("DaphneVBISink: output_path is empty");
        return false;
    }

    // Validate inputs
    if (inputs.empty()) {
        trigger_status_ = "Error: No input connected";
        ORC_LOG_ERROR("DaphneVBISink: No input provided");
        return false;
    }

    // Get input representation
    auto representation = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    if (!representation) {
        trigger_status_ = "Error: Input is not a video field representation";
        ORC_LOG_ERROR("DaphneVBISink: Input is not VideoFieldRepresentation");
        return false;
    }

    // Write .VBI binary file
    ORC_LOG_INFO("DaphneVBISink: Writing to '{}'", output_path);
    // Clear previous observations to avoid mixing runs
    observation_context.clear();

    std::shared_ptr<IDaphneVBISinkStageDeps> deps = this->factories_->get_instance_stage_factories()->CreateInstanceDaphneVBISinkStageDeps(progress_callback_, is_processing_, cancel_requested_);
    bool success = deps->write_vbi(representation.get(), output_path, observation_context);

    if (success) {
        auto range = representation->field_range();
        trigger_status_ = "Exported " + std::to_string(range.size()) + " fields to " + output_path;
        ORC_LOG_DEBUG("DaphneVBISink: Trigger completed successfully");
    } else {
        trigger_status_ = "Error: Failed to write output files";
        ORC_LOG_ERROR("DaphneVBISink: Trigger failed");
    }

    is_processing_.store(false);
    return success;
}

std::string DaphneVBISinkStage::get_trigger_status() const
{
    return trigger_status_;
}

} // orc