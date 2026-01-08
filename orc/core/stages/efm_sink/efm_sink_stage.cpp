/*
 * File:        efm_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     EFM Data Sink Stage - writes EFM t-values to raw file
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "efm_sink_stage.h"
#include "logging.h"
#include "tbc_metadata.h"
#include "buffered_file_io.h"
#include <stage_registry.h>
#include <stdexcept>
#include <fstream>

namespace orc {

// Register this stage with the registry
ORC_REGISTER_STAGE(EFMSinkStage)

// Force linker to include this object file
void force_link_EFMSinkStage() {}

EFMSinkStage::EFMSinkStage() = default;

NodeTypeInfo EFMSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::SINK,
        "EFMSink",
        "EFM Data Sink",
        "Extracts EFM t-values and writes to raw binary file",
        1, 1,  // One input
        0, 0,  // No outputs (sink)
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> EFMSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters
) {
    // Sink stages don't produce outputs in execute()
    // Actual work happens in trigger()
    (void)inputs;
    (void)parameters;
    return {};
}

std::vector<ParameterDescriptor> EFMSinkStage::get_parameter_descriptors(VideoSystem project_format) const {
    (void)project_format;
    std::vector<ParameterDescriptor> descriptors;
    
    // output_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "output_path";
        desc.display_name = "Output EFM File";
        desc.description = "Path to output EFM data file (raw t-values)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = true;
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".efm";
        descriptors.push_back(desc);
    }
    
    return descriptors;
}

std::map<std::string, ParameterValue> EFMSinkStage::get_parameters() const {
    return parameters_;
}

bool EFMSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

bool EFMSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters
) {
    is_processing_.store(true);
    cancel_requested_.store(false);
    
    try {
        // Validate inputs
        if (inputs.empty()) {
            throw std::runtime_error("EFM sink requires one input (VideoFieldRepresentation)");
        }
        
        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            throw std::runtime_error("Input must be a VideoFieldRepresentation");
        }
        
        // Check if VFR has EFM
        if (!vfr->has_efm()) {
            throw std::runtime_error("Input VFR does not have EFM data (no EFM file specified in source?)");
        }
        
        // Get parameters
        auto output_path_it = parameters.find("output_path");
        if (output_path_it == parameters.end()) {
            throw std::runtime_error("output_path parameter is required");
        }
        std::string output_path = std::get<std::string>(output_path_it->second);
        
        ORC_LOG_INFO("EFMSink: Writing EFM data to {}", output_path);
        
        // Calculate field range
        auto field_range = vfr->field_range();
        FieldID start_field = field_range.start;
        FieldID end_field = field_range.end;
        
        uint64_t total_fields = end_field.value() - start_field.value();
        ORC_LOG_DEBUG("  Processing {} fields", total_fields);
        
        // First pass: count total t-values
        uint64_t total_tvalues = 0;
        for (FieldID fid = start_field; fid < end_field; ++fid) {
            total_tvalues += vfr->get_efm_sample_count(fid);
        }
        
        ORC_LOG_DEBUG("  Total EFM t-values: {}", total_tvalues);
        
        if (total_tvalues == 0) {
            throw std::runtime_error("No EFM t-values found in field range");
        }
        
        // Open output file with buffered writer (4MB buffer for optimal performance)
        BufferedFileWriter<uint8_t> writer(4 * 1024 * 1024);
        if (!writer.open(output_path)) {
            throw std::runtime_error("Failed to open output file: " + output_path);
        }
        
        // Second pass: write EFM data using buffered writer
        uint64_t tvalues_written = 0;
        uint64_t invalid_tvalue_count = 0;
        
        for (FieldID fid = start_field; fid < end_field; ++fid) {
            if (cancel_requested_.load()) {
                writer.close();
                last_status_ = "Cancelled by user";
                ORC_LOG_WARN("EFMSink: {}", last_status_);
                is_processing_.store(false);
                return false;
            }
            
            // Get EFM t-values for this field
            auto tvalues = vfr->get_efm_samples(fid);
            if (!tvalues.empty()) {
                // Validate t-values are in valid range [3, 11]
                for (const auto& tval : tvalues) {
                    if (tval < 3 || tval > 11) {
                        invalid_tvalue_count++;
                    }
                }
                
                // Write t-values using buffered writer (automatically batches writes)
                writer.write(tvalues);
                tvalues_written += tvalues.size();
            }
            
            // Update progress
            uint64_t current_field = fid.value() - start_field.value();
            if (current_field % 10 == 0 && progress_callback_) {
                progress_callback_(current_field, total_fields, 
                                 "Writing EFM field " + std::to_string(current_field) + "/" + std::to_string(total_fields));
            }
            if (current_field % 100 == 0) {
                double progress = static_cast<double>(current_field) / total_fields * 100.0;
                ORC_LOG_DEBUG("EFMSink: Progress {:.1f}%", progress);
            }
        }
        
        writer.close();
        
        ORC_LOG_INFO("EFMSink: Successfully wrote {} t-values to {}", 
                    tvalues_written, output_path);
        ORC_LOG_DEBUG("  Expected t-values: {}, Actual t-values: {}, Match: {}", 
                     total_tvalues, tvalues_written, total_tvalues == tvalues_written ? "YES" : "NO");
        
        if (invalid_tvalue_count > 0) {
            ORC_LOG_WARN("EFMSink: Found {} invalid t-values (outside range [3, 11])", 
                        invalid_tvalue_count);
        }
        
        last_status_ = "Success: " + std::to_string(tvalues_written) + " t-values written";
        is_processing_.store(false);
        return true;
        
    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("EFMSink: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

std::string EFMSinkStage::get_trigger_status() const {
    return last_status_;
}

} // namespace orc
