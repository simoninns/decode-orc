/*
 * File:        ld_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     ld-decode sink Stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "ld_sink_stage.h"
#include "stage_registry.h"
#include "preview_renderer.h"
#include "preview_helpers.h"
#include "tbc_metadata_writer.h"
#include "observation_history.h"
#include "biphase_observer.h"
#include "vitc_observer.h"
#include "closed_caption_observer.h"
#include "video_id_observer.h"
#include "fm_code_observer.h"
#include "white_flag_observer.h"
#include "vits_observer.h"
#include "burst_level_observer.h"
#include "buffered_file_io.h"
#include "logging.h"
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace orc {

// Register stage with registry
ORC_REGISTER_STAGE(LDSinkStage)

// Force linker to include this object file
void force_link_LDSinkStage() {}

LDSinkStage::LDSinkStage()
    : output_path_("")
{
}

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
    const std::map<std::string, ParameterValue>& parameters [[maybe_unused]])
{
    // Cache input for preview rendering
    if (!inputs.empty()) {
        cached_input_ = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    }
    
    // Sink stages don't produce outputs during normal execution
    // They are triggered manually to write data
    ORC_LOG_DEBUG("LDSink execute called (cached input for preview)");
    return {};  // No outputs
}

std::vector<ParameterDescriptor> LDSinkStage::get_parameter_descriptors(VideoSystem project_format) const
{
    (void)project_format;  // Unused - LD sink works with all formats
    return {
        ParameterDescriptor{
            "output_path",
            "TBC Output Path",
            "Path to output TBC file (metadata will be written to .db)",
            ParameterType::FILE_PATH,
            ParameterConstraints{},  // No constraints for file paths
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
    const std::map<std::string, ParameterValue>& parameters)
{
    ORC_LOG_DEBUG("LDSink: Trigger started");
    trigger_status_ = "Starting export...";
    is_processing_.store(true);
    cancel_requested_.store(false);
    
    // Validate parameters
    auto it = parameters.find("output_path");
    if (it == parameters.end() || !std::holds_alternative<std::string>(it->second)) {
        trigger_status_ = "Error: No output path specified";
        ORC_LOG_ERROR("LDSink: No output_path parameter");
        return false;
    }
    
    std::string output_path = std::get<std::string>(it->second);
    if (output_path.empty()) {
        trigger_status_ = "Error: Output path is empty";
        ORC_LOG_ERROR("LDSink: output_path is empty");
        return false;
    }
    
    // Validate inputs
    if (inputs.empty()) {
        trigger_status_ = "Error: No input connected";
        ORC_LOG_ERROR("LDSink: No input provided");
        return false;
    }
    
    // Get input representation
    auto representation = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    if (!representation) {
        trigger_status_ = "Error: Input is not a video field representation";
        ORC_LOG_ERROR("LDSink: Input is not VideoFieldRepresentation");
        return false;
    }
    
    // Write TBC and metadata
    ORC_LOG_INFO("LDSink: Writing to '{}'", output_path);
    bool success = write_tbc_and_metadata(representation.get(), output_path);
    
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

bool LDSinkStage::write_tbc_and_metadata(
    const VideoFieldRepresentation* representation,
    const std::string& tbc_path)
{
    // Ensure the path has .tbc extension
    std::string final_tbc_path = tbc_path;
    const std::string tbc_ext = ".tbc";
    if (tbc_path.length() < tbc_ext.length() || 
        tbc_path.compare(tbc_path.length() - tbc_ext.length(), tbc_ext.length(), tbc_ext) != 0) {
        final_tbc_path += ".tbc";
        ORC_LOG_DEBUG("Added .tbc extension: {}", final_tbc_path);
    }
    
    std::string db_path = final_tbc_path + ".db";
    
    // Get field count early for progress reporting
    auto range = representation->field_range();
    size_t field_count = range.size();
    
    // Show initial progress
    if (progress_callback_) {
        progress_callback_(0, field_count, "Preparing export...");
    }
    
    try {
        ORC_LOG_DEBUG("Opening TBC file for writing: {}", final_tbc_path);
        ORC_LOG_DEBUG("Opening metadata database: {}", db_path);
        
        // Open TBC file with buffered writer (16MB buffer for large field writes)
        BufferedFileWriter<uint16_t> tbc_writer(16 * 1024 * 1024);
        if (!tbc_writer.open(final_tbc_path)) {
            ORC_LOG_ERROR("Failed to open TBC file for writing: {}", final_tbc_path);
            return false;
        }
        
        // Open metadata database
        TBCMetadataWriter metadata_writer;
        if (!metadata_writer.open(db_path)) {
            ORC_LOG_ERROR("Failed to open metadata database for writing: {}", db_path);
            return false;
        }
        
        // Get video parameters and write them
        auto video_params = representation->get_video_parameters();
        if (!video_params) {
            ORC_LOG_ERROR("No video parameters available");
            return false;
        }
        video_params->decoder = "orc";
        if (!metadata_writer.write_video_parameters(*video_params)) {
            ORC_LOG_ERROR("Failed to write video parameters");
            return false;
        }
        
        // Create all observers
        std::vector<std::shared_ptr<Observer>> observers;
        observers.push_back(std::make_shared<BiphaseObserver>());
        observers.push_back(std::make_shared<VitcObserver>());
        observers.push_back(std::make_shared<ClosedCaptionObserver>());
        observers.push_back(std::make_shared<VideoIdObserver>());
        observers.push_back(std::make_shared<FmCodeObserver>());
        observers.push_back(std::make_shared<WhiteFlagObserver>());
        observers.push_back(std::make_shared<VITSQualityObserver>());
        observers.push_back(std::make_shared<BurstLevelObserver>());
        
        // Build sorted list of field IDs
        std::vector<FieldID> field_ids;
        field_ids.reserve(field_count);
        for (FieldID field_id = range.start; field_id < range.end; field_id = field_id + 1) {
            if (representation->has_field(field_id)) {
                field_ids.push_back(field_id);
            }
        }
        std::sort(field_ids.begin(), field_ids.end());
        
        ORC_LOG_DEBUG("Processing {} fields (TBC + metadata) in single pass", field_ids.size());
        
        // Begin transaction for metadata writes
        metadata_writer.begin_transaction();
        
        // Observation history for observers that need previous field context
        ObservationHistory history;
        
        // Pre-populate history from source representations
        for (FieldID field_id : field_ids) {
            auto source_observations = representation->get_observations(field_id);
            if (!source_observations.empty()) {
                history.add_observations(field_id, source_observations);
            }
        }
        
        size_t fields_processed = 0;
        
        // Single pass: write TBC data and process metadata for each field
        for (FieldID field_id : field_ids) {
            // Check for cancellation
            if (cancel_requested_.load()) {
                metadata_writer.commit_transaction();
                metadata_writer.close();
                tbc_writer.close();
                ORC_LOG_WARN("LDSink: Export cancelled by user");
                is_processing_.store(false);
                return false;
            }
            
            // ===== Write TBC data =====
            auto descriptor = representation->get_descriptor(field_id);
            if (!descriptor) {
                ORC_LOG_WARN("No descriptor for field {}, skipping", field_id.value());
                continue;
            }
            
            size_t expected_lines = descriptor->height;
            size_t line_width = descriptor->width;
            
            // Buffer for accumulating the entire field before writing
            std::vector<uint16_t> field_buffer;
            field_buffer.reserve(expected_lines * line_width);
            
            // Accumulate all lines of the field into buffer
            for (size_t line_num = 0; line_num < expected_lines; ++line_num) {
                const uint16_t* line_data = representation->get_line(field_id, line_num);
                if (!line_data) {
                    ORC_LOG_WARN("Field {} line {} has no data", field_id.value(), line_num);
                    field_buffer.insert(field_buffer.end(), line_width, 0);
                } else {
                    field_buffer.insert(field_buffer.end(), line_data, line_data + line_width);
                }
            }
            
            // Write the entire field to TBC
            tbc_writer.write(field_buffer);
            
            // ===== Write metadata =====
            // Create minimal field record
            FieldMetadata field_meta;
            field_meta.seq_no = field_id.value() + 1;  // seq_no is 1-based
            
            // Check for field parity HINT
            auto parity_hint = representation->get_field_parity_hint(field_id);
            if (parity_hint.has_value()) {
                field_meta.is_first_field = parity_hint->is_first_field;
            } else {
                field_meta.is_first_field = false;
            }
            
            // Check for field phase HINT
            auto phase_hint = representation->get_field_phase_hint(field_id);
            if (phase_hint.has_value()) {
                field_meta.field_phase_id = phase_hint->field_phase_id;
            }
            
            metadata_writer.write_field_metadata(field_meta);
            
            // Write dropout hints
            auto dropout_hints = representation->get_dropout_hints(field_id);
            for (const auto& hint : dropout_hints) {
                DropoutInfo dropout;
                dropout.line = hint.line;
                dropout.start_sample = hint.start_sample;
                dropout.end_sample = hint.end_sample;
                metadata_writer.write_dropout(field_id, dropout);
            }
            
            // Run all observers on this field
            for (const auto& observer : observers) {
                auto observations = observer->process_field(*representation, field_id, history);
                metadata_writer.write_observations(field_id, observations);
                
                // Add observations to history for subsequent observers
                auto current_field_obs = history.get_observations(field_id);
                current_field_obs.insert(current_field_obs.end(), 
                                        observations.begin(), observations.end());
                history.add_observations(field_id, current_field_obs);
            }
            
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
        
        // Commit metadata transaction and close files
        metadata_writer.commit_transaction();
        metadata_writer.close();
        tbc_writer.close();
        
        ORC_LOG_DEBUG("Successfully exported {} fields", fields_processed);
        return true;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Exception during export: {}", e.what());
        return false;
    }
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
