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
#include "logging.h"
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace orc {

// Register stage with registry
static StageRegistration reg([]() {
    return std::make_shared<LDSinkStage>();
});

LDSinkStage::LDSinkStage()
    : tbc_path_("")
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
            "tbc_path",
            "TBC Output Path",
            "Path to output TBC file (metadata will be written to .db)",
            ParameterType::FILE_PATH,
            ParameterConstraints{}  // No constraints for file paths
        }
    };
}

std::map<std::string, ParameterValue> LDSinkStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["tbc_path"] = tbc_path_;
    return params;
}

bool LDSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    auto it = params.find("tbc_path");
    if (it != params.end()) {
        if (std::holds_alternative<std::string>(it->second)) {
            tbc_path_ = std::get<std::string>(it->second);
            ORC_LOG_DEBUG("LDSink: tbc_path set to '{}'", tbc_path_);
        } else {
            ORC_LOG_ERROR("LDSink: tbc_path parameter must be string");
            return false;
        }
    }
    
    return true;
}

bool LDSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    ORC_LOG_INFO("LDSink: Trigger started");
    trigger_status_ = "Starting export...";
    
    // Validate parameters
    auto it = parameters.find("tbc_path");
    if (it == parameters.end() || !std::holds_alternative<std::string>(it->second)) {
        trigger_status_ = "Error: No output path specified";
        ORC_LOG_ERROR("LDSink: No tbc_path parameter");
        return false;
    }
    
    std::string output_path = std::get<std::string>(it->second);
    if (output_path.empty()) {
        trigger_status_ = "Error: Output path is empty";
        ORC_LOG_ERROR("LDSink: tbc_path is empty");
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
        ORC_LOG_INFO("LDSink: Trigger completed successfully");
    } else {
        trigger_status_ = "Error: Failed to write output files";
        ORC_LOG_ERROR("LDSink: Trigger failed");
    }
    
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
    
    // Write TBC file
    if (!write_tbc_file(representation, final_tbc_path)) {
        return false;
    }
    
    // Write metadata file
    std::string db_path = final_tbc_path + ".db";
    if (!write_metadata_file(representation, db_path)) {
        return false;
    }
    
    return true;
}

bool LDSinkStage::write_tbc_file(
    const VideoFieldRepresentation* representation,
    const std::string& tbc_path)
{
    try {
        ORC_LOG_INFO("Opening TBC file for writing: {}", tbc_path);
        
        // Open output file
        std::ofstream tbc_file(tbc_path, std::ios::binary | std::ios::trunc);
        if (!tbc_file) {
            ORC_LOG_ERROR("Failed to open TBC file for writing: {}", tbc_path);
            return false;
        }
        
        auto range = representation->field_range();
        size_t field_count = range.size();
        size_t fields_written = 0;
        
        ORC_LOG_INFO("Writing {} fields to TBC file (range: {} to {})", field_count, range.start.value(), range.end.value());
        
        // Iterate through all fields and write them
        for (FieldID field_id = range.start; field_id < range.end; field_id = field_id + 1) {
            if (!representation->has_field(field_id)) {
                ORC_LOG_WARN("Field {} not available, skipping", field_id.value());
                continue;
            }
            
            // Get field metadata to determine line count
            auto descriptor = representation->get_descriptor(field_id);
            if (!descriptor) {
                ORC_LOG_WARN("No descriptor for field {}, skipping", field_id.value());
                continue;
            }
            
            size_t expected_lines = descriptor->height;
            
            // Write each line of the field
            for (size_t line_num = 0; line_num < expected_lines; ++line_num) {
                const uint16_t* line_data = representation->get_line(field_id, line_num);
                if (!line_data) {
                    ORC_LOG_WARN("Field {} line {} has no data", field_id.value(), line_num);
                    // Write zeros for missing lines
                    std::vector<uint16_t> zero_line(1135, 0);  // Default line length
                    tbc_file.write(reinterpret_cast<const char*>(zero_line.data()), 
                                 zero_line.size() * sizeof(uint16_t));
                } else {
                    // Write the line - we know the width from the descriptor
                    tbc_file.write(reinterpret_cast<const char*>(line_data),
                                 descriptor->width * sizeof(uint16_t));
                }
            }
            
            fields_written++;
            
            // Log progress every 10 fields
            if (fields_written % 10 == 0) {
                ORC_LOG_DEBUG("Written {}/{} fields ({:.1f}%)", fields_written, field_count, 
                            (fields_written * 100.0) / field_count);
            }
        }
        
        tbc_file.close();
        ORC_LOG_INFO("Successfully wrote {} fields to TBC file", fields_written);
        return true;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Exception writing TBC file: {}", e.what());
        return false;
    }
}

bool LDSinkStage::write_metadata_file(
    const VideoFieldRepresentation* representation,
    const std::string& db_path)
{
    try {
        ORC_LOG_INFO("Writing SQLite metadata to {}", db_path);
        
        // Create metadata writer
        TBCMetadataWriter writer;
        if (!writer.open(db_path)) {
            ORC_LOG_ERROR("Failed to open metadata database for writing: {}", db_path);
            return false;
        }
        
        // Get video parameters
        auto video_params = representation->get_video_parameters();
        if (!video_params) {
            ORC_LOG_ERROR("No video parameters available");
            return false;
        }
        
        // Set decoder to "orc"
        video_params->decoder = "orc";
        
        // Write video parameters (creates capture record)
        if (!writer.write_video_parameters(*video_params)) {
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
        // Note: FieldParityObserver removed - field parity comes from hints only
        // Note: PALPhaseObserver removed - PAL phase comes from hints only
        // (TBC files have normalized sync patterns, making field parity detection impossible)
        
        ORC_LOG_INFO("Running observers on all fields...");
        
        // Process all fields with observers
        auto range = representation->field_range();
        size_t field_count = range.size();
        size_t fields_processed = 0;
        
        // Build list of field IDs in sorted order
        // This ensures consistent processing regardless of field map reordering
        std::vector<FieldID> field_ids;
        field_ids.reserve(field_count);
        for (FieldID field_id = range.start; field_id < range.end; field_id = field_id + 1) {
            if (representation->has_field(field_id)) {
                field_ids.push_back(field_id);
            }
        }
        // Sort by field_id to ensure deterministic order
        std::sort(field_ids.begin(), field_ids.end());
        
        // Begin transaction for bulk inserts
        writer.begin_transaction();
        
        // Observation history for observers that need previous field context
        // Processing fields in field_id order ensures history is built correctly
        // even if fields were reordered by upstream stages
        ObservationHistory history;
        
        // Pre-populate history from source representations
        // This enables correct behavior when fields come from multiple sources
        // (e.g., after a merge stage that interleaves two inputs)
        for (FieldID field_id : field_ids) {
            auto source_observations = representation->get_observations(field_id);
            if (!source_observations.empty()) {
                history.add_observations(field_id, source_observations);
            }
        }
        
        for (FieldID field_id : field_ids) {
            
            // Create minimal field record
            // Note: Field-level metadata (sync_confidence, median_burst_ire, field_phase_id, is_first_field)
            // is NOT copied from input - it should flow through the pipeline via observers/hints.
            // The sink only writes what can be determined from the current representation.
            FieldMetadata field_meta;
            field_meta.seq_no = field_id.value() + 1;  // seq_no is 1-based
            
            // Check for field parity HINT (from upstream processor like ld-decode)
            // Hints are preferred over observations as they represent authoritative metadata
            auto parity_hint = representation->get_field_parity_hint(field_id);
            if (parity_hint.has_value()) {
                field_meta.is_first_field = parity_hint->is_first_field;
                ORC_LOG_DEBUG("Writing field {} (seq_no={}) with field parity HINT: is_first_field={}", 
                             field_id.value(), field_meta.seq_no, field_meta.is_first_field.value());
            } else {
                field_meta.is_first_field = false;  // Default if no hint
                ORC_LOG_DEBUG("Writing field {} (seq_no={}) - no parity hint", 
                             field_id.value(), field_meta.seq_no);
            }
            
            // Check for field phase HINT (from upstream processor like ld-decode)
            // Works for both PAL and NTSC
            auto phase_hint = representation->get_field_phase_hint(field_id);
            if (phase_hint.has_value()) {
                field_meta.field_phase_id = phase_hint->field_phase_id;
                ORC_LOG_DEBUG("Writing field {} (seq_no={}) with phase HINT: field_phase_id={}", 
                             field_id.value(), field_meta.seq_no, field_meta.field_phase_id.value());
            } else {
                ORC_LOG_DEBUG("Writing field {} (seq_no={}) - no phase hint", 
                             field_id.value(), field_meta.seq_no);
            }
            
            // Observers will populate VBI, VITC, VITS, etc. from the actual field data
            writer.write_field_metadata(field_meta);
            
            // Get dropout hints and write them
            auto dropout_hints = representation->get_dropout_hints(field_id);
            for (const auto& hint : dropout_hints) {
                DropoutInfo dropout;
                dropout.line = hint.line;
                dropout.start_sample = hint.start_sample;
                dropout.end_sample = hint.end_sample;
                writer.write_dropout(field_id, dropout);
            }
            
            // Run all observers on this field, passing the observation history
            // Note: Observations are added to history incrementally so later observers
            // can use results from earlier observers in the same field
            for (const auto& observer : observers) {
                auto observations = observer->process_field(*representation, field_id, history);
                writer.write_observations(field_id, observations);
                
                // Add observations to history immediately so subsequent observers can use them
                auto current_field_obs = history.get_observations(field_id);
                current_field_obs.insert(current_field_obs.end(), 
                                        observations.begin(), observations.end());
                history.add_observations(field_id, current_field_obs);
            }
            
            fields_processed++;
            
            // Log progress every 50 fields
            if (fields_processed % 50 == 0) {
                ORC_LOG_DEBUG("Processed {}/{} fields ({:.1f}%)", fields_processed, field_count, 
                            (fields_processed * 100.0) / field_count);
            }
        }
        
        // Commit transaction
        writer.commit_transaction();
        writer.close();
        
        ORC_LOG_INFO("Successfully wrote metadata for {} fields", fields_processed);
        return true;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Exception writing metadata file: {}", e.what());
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
