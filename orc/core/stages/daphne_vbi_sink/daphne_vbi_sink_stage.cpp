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
#include "tbc_metadata_writer.h"
#include "buffered_file_io.h"
#include "logging.h"
#include "biphase_observer.h"
#include "white_flag_observer.h"
#include <filesystem>
#include <algorithm>
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
        VideoFormatCompatibility::ALL
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
    ObservationContext& observation_context)
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
    bool success = write_vbi(representation.get(), output_path, observation_context);

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

bool DaphneVBISinkStage::write_vbi(
    const VideoFieldRepresentation* representation,
    const std::string& vbi_path,
    ObservationContext& observation_context)
{
    // Ensure the path has .vbi extension
    std::string final_vbi_path = vbi_path;
    const std::string tbc_ext = ".vbi";
    if (vbi_path.length() < tbc_ext.length() ||
        vbi_path.compare(vbi_path.length() - tbc_ext.length(), tbc_ext.length(), tbc_ext) != 0) {
        final_vbi_path += ".vbi";
        ORC_LOG_DEBUG("Added .vbi extension: {}", final_vbi_path);
    }

    // Get field count early for progress reporting
    auto range = representation->field_range();
    size_t field_count = range.size();

    // Show initial progress
    if (progress_callback_) {
        progress_callback_(0, field_count, "Preparing export...");
    }

    try {
        ORC_LOG_DEBUG("Opening VBI file for writing: {}", final_vbi_path);

        // Open VBI file with buffered writer (1MB buffer chosen arbitrarily)
        std::unique_ptr<IFileWriter<uint8_t>> vbi_writer = factories_->create_instance_buffered_file_writer_uint8(1 * 1024 * 1024);
        if (!vbi_writer->open(final_vbi_path)) {
            ORC_LOG_ERROR("Failed to open VBI file for writing: {}", final_vbi_path);
            return false;
        }

        // Create VBI writer instance (TODO : use abstract factory in the future?)
        DaphneVBIWriterUtil vbi_writer_util(vbi_writer.get());
        vbi_writer_util.write_header();  // header is required at the beginning of .VBI file

        // Get video parameters
        auto video_params = representation->get_video_parameters();
        if (!video_params) {
            ORC_LOG_ERROR("No video parameters available");
            return false;
        }
        video_params->decoder = "orc";

        // Build sorted list of field IDs
        std::vector<FieldID> field_ids;
        field_ids.reserve(field_count);
        for (FieldID field_id = range.start; field_id < range.end; field_id = field_id + 1) {
            if (representation->has_field(field_id)) {
                field_ids.push_back(field_id);
            }
        }
        std::sort(field_ids.begin(), field_ids.end());

        ORC_LOG_DEBUG("Processing {} fields in single pass", field_ids.size());

        // Create vector of observers; we only care about biphase and white flag
        std::vector<std::shared_ptr<Observer>> observers;
        observers.push_back(std::make_shared<BiphaseObserver>());
        observers.push_back(std::make_shared<WhiteFlagObserver>());

        ORC_LOG_DEBUG("Instantiated {} observers for VBI data extraction", observers.size());

        size_t fields_processed = 0;

        // Single pass: populate observations and write VBI for each field
        for (FieldID field_id : field_ids) {
            // Check for cancellation
            if (cancel_requested_.load()) {
                vbi_writer->close();
                ORC_LOG_WARN("DaphneVBISink: Export cancelled by user");
                is_processing_.store(false);
                return false;
            }

            // ===== Write VBI data =====
            auto descriptor = representation->get_descriptor(field_id);
            if (!descriptor) {
                ORC_LOG_WARN("No descriptor for field {}, skipping", field_id.value());
                continue;
            }

            // ===== Run observers to populate observation context =====
            for (const auto& observer : observers) {
                observer->process_field(*representation, field_id, observation_context);
            }

            // Write observations to VBI file
            vbi_writer_util.write_observations(field_id, &observation_context);

            fields_processed++;

            // Update progress callback every 10 fields
            if (fields_processed % 10 == 0) {
                if (progress_callback_) {
                    progress_callback_(fields_processed, field_count,
                                     "Exporting field " + std::to_string(fields_processed) + "/" + std::to_string(field_count));
                }
            }

            // Log progress every 50 fields
            if (fields_processed % 50 == 0) {
                ORC_LOG_DEBUG("Exported {}/{} fields ({:.1f}%)", fields_processed, field_count,
                            (fields_processed * 100.0) / field_count);
            }
        }

        vbi_writer->close();

        ORC_LOG_DEBUG("Successfully exported {} fields", fields_processed);
        return true;

    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Exception during export: {}", e.what());
        return false;
    }
}

} // orc