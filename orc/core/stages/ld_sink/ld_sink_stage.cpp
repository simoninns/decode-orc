/*
 * File:        ld_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     LaserDisc Sink Stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "ld_sink_stage.h"
#include "stage_registry.h"
#include "logging.h"
#include <fstream>
#include <filesystem>

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
        "LaserDisc Sink",            // display_name
        "Writes TBC fields and metadata to disk. Trigger to export all fields.",  // description
        1,                           // min_inputs
        1,                           // max_inputs
        0,                           // min_outputs
        0                            // max_outputs
    };
}

std::vector<ArtifactPtr> LDSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    // Sink stages don't produce outputs during normal execution
    // They are triggered manually to write data
    ORC_LOG_DEBUG("LDSink execute called (no-op - use trigger to write)");
    return {};  // No outputs
}

std::vector<ParameterDescriptor> LDSinkStage::get_parameter_descriptors() const
{
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
        ORC_LOG_INFO("Writing metadata to {}", db_path);
        
        // Get video parameters
        auto video_params = representation->get_video_parameters();
        if (!video_params) {
            ORC_LOG_ERROR("No video parameters available");
            return false;
        }
        
        // For now, create a minimal JSON file with video parameters
        // Full metadata writing requires implementing JSON serialization
        std::ofstream meta_file(db_path);
        if (!meta_file) {
            ORC_LOG_ERROR("Failed to open metadata file for writing: {}", db_path);
            return false;
        }
        
        // Write minimal JSON structure
        meta_file << "{\n";
        meta_file << "  \"videoParameters\": {\n";
        meta_file << "    \"system\": \"" << (video_params->system == VideoSystem::PAL ? "PAL" : "NTSC") << "\",\n";
        meta_file << "    \"fieldWidth\": " << video_params->field_width << ",\n";
        meta_file << "    \"fieldHeight\": " << video_params->field_height << ",\n";
        meta_file << "    \"numberOfSequentialFields\": " << video_params->number_of_sequential_fields << "\n";
        meta_file << "  },\n";
        meta_file << "  \"fields\": {}\n";
        meta_file << "}\n";
        
        meta_file.close();
        
        ORC_LOG_INFO("Successfully wrote metadata file");
        return true;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Exception writing metadata file: {}", e.what());
        return false;
    }
}

} // namespace orc
