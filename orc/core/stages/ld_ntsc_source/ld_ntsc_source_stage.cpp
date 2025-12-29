/*
 * File:        ld_ntsc_source_stage.cpp
 * Module:      orc-core
 * Purpose:     LaserDisc NTSC source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "ld_ntsc_source_stage.h"
#include "logging.h"
#include "preview_renderer.h"
#include "preview_helpers.h"
#include "observation_wrapper_representation.h"
#include "biphase_observer.h"
#include "observation_history.h"
#include <stage_registry.h>
#include <stdexcept>
#include <fstream>

namespace orc {

// Register this stage with the registry
ORC_REGISTER_STAGE(LDNTSCSourceStage)

// Force linker to include this object file
void force_link_LDNTSCSourceStage() {}

std::vector<ArtifactPtr> LDNTSCSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters
) {
    // Source stage should have no inputs
    if (!inputs.empty()) {
        throw std::runtime_error("LDNTSCSource stage should have no inputs");
    }

    // Get input_path parameter
    auto input_path_it = parameters.find("input_path");
    if (input_path_it == parameters.end() || std::get<std::string>(input_path_it->second).empty()) {
        // No file path configured - return empty artifact (0 fields)
        // This allows the node to exist in the DAG without a file, acting as a placeholder
        ORC_LOG_DEBUG("LDNTSCSource: No input_path configured, returning empty output");
        return {};
    }
    std::string input_path = std::get<std::string>(input_path_it->second);

    // Get db_path parameter (optional)
    std::string db_path;
    auto db_path_it = parameters.find("db_path");
    if (db_path_it != parameters.end()) {
        db_path = std::get<std::string>(db_path_it->second);
    } else {
        // Default: input_path + ".db"
        db_path = input_path + ".db";
    }

    // Check cache
    if (cached_representation_ && cached_input_path_ == input_path) {
        ORC_LOG_DEBUG("LDNTSCSource: Using cached representation for {}", input_path);
        return {cached_representation_};
    }

    // Load the TBC file
    ORC_LOG_INFO("LDNTSCSource: Loading TBC file: {}", input_path);
    ORC_LOG_DEBUG("  Database: {}", db_path);
    
    try {
        auto tbc_representation = create_tbc_representation(input_path, db_path);
        if (!tbc_representation) {
            throw std::runtime_error("Failed to load TBC file (validation failed - see logs above)");
        }
        
        // Get video parameters for logging
        auto video_params = tbc_representation->get_video_parameters();
        if (!video_params) {
            throw std::runtime_error("No video parameters found in TBC file");
        }
        
        std::string system_str;
        switch (video_params->system) {
            case VideoSystem::PAL: system_str = "PAL"; break;
            case VideoSystem::PAL_M: system_str = "PAL-M"; break;
            case VideoSystem::NTSC: system_str = "NTSC"; break;
            default: system_str = "UNKNOWN"; break;
        }
        ORC_LOG_INFO("  Decoder: {}", video_params->decoder);
        ORC_LOG_INFO("  System: {}", system_str);
        ORC_LOG_INFO("  Fields: {} ({}x{} pixels)", 
                    video_params->number_of_sequential_fields,
                    video_params->field_width, 
                    video_params->field_height);
        
        // Check decoder
        if (video_params->decoder != "ld-decode") {
            throw std::runtime_error(
                "TBC file was not created by ld-decode (decoder: " + 
                video_params->decoder + "). Use the appropriate source type."
            );
        }
        
        // Check system
        if (video_params->system != VideoSystem::NTSC) {
            throw std::runtime_error(
                "TBC file is not NTSC format. Use 'Add LD PAL Source' for PAL files."
            );
        }
        
        // Run observers on all fields to extract VBI and other metadata
        ORC_LOG_INFO("LDNTSCSource: Running observers on all fields...");
        auto biphase_observer = std::make_shared<BiphaseObserver>();
        ObservationHistory history;
        std::map<FieldID, std::vector<std::shared_ptr<Observation>>> observations_map;
        
        auto field_range = tbc_representation->field_range();
        for (FieldID fid = field_range.start; fid < field_range.end; ++fid) {
            auto observations = biphase_observer->process_field(*tbc_representation, fid, history);
            if (!observations.empty()) {
                observations_map[fid] = observations;
                history.add_observations(fid, observations);
            }
        }
        
        ORC_LOG_INFO("LDNTSCSource: Extracted observations for {} fields", observations_map.size());
        
        // Wrap the representation with observations
        cached_representation_ = std::make_shared<ObservationWrapperRepresentation>(
            tbc_representation, 
            observations_map
        );
        cached_input_path_ = input_path;
        
        return {cached_representation_};
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to load NTSC TBC file '") + input_path + "': " + e.what()
        );
    }
}

std::vector<ParameterDescriptor> LDNTSCSourceStage::get_parameter_descriptors(VideoSystem project_format) const
{
    (void)project_format;  // Unused - source stages don't need project format
    std::vector<ParameterDescriptor> descriptors;
    
    // input_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "input_path";
        desc.display_name = "TBC File Path";
        desc.description = "Path to the NTSC .tbc file from ld-decode (database file is automatically located)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = false;  // Optional - source provides 0 fields until path is set
        descriptors.push_back(desc);
    }
    
    return descriptors;
}

std::map<std::string, ParameterValue> LDNTSCSourceStage::get_parameters() const
{
    return parameters_;
}

bool LDNTSCSourceStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Validate that input_path has correct type if present
    auto input_path_it = params.find("input_path");
    if (input_path_it != params.end() && !std::holds_alternative<std::string>(input_path_it->second)) {
        return false;
    }
    
    parameters_ = params;
    return true;
}

std::optional<StageReport> LDNTSCSourceStage::generate_report() const {
    StageReport report;
    report.summary = "NTSC Source Status";
    
    // Get input_path from parameters
    std::string input_path;
    auto input_path_it = parameters_.find("input_path");
    if (input_path_it != parameters_.end()) {
        input_path = std::get<std::string>(input_path_it->second);
    }
    
    if (input_path.empty()) {
        report.items.push_back({"Source File", "Not configured"});
        report.items.push_back({"Status", "No TBC file path set"});
        return report;
    }
    
    report.items.push_back({"Source File", input_path});
    
    // Get db_path
    std::string db_path;
    auto db_path_it = parameters_.find("db_path");
    if (db_path_it != parameters_.end()) {
        db_path = std::get<std::string>(db_path_it->second);
    } else {
        db_path = input_path + ".db";
    }
    
    // Try to load the file to get actual information
    try {
        auto representation = create_tbc_representation(input_path, db_path);
        if (representation) {
            auto video_params = representation->get_video_parameters();
            
            report.items.push_back({"Status", "File accessible"});
            
            if (video_params) {
                report.items.push_back({"Decoder", video_params->decoder});
                report.items.push_back({"Video System", "NTSC"});
                report.items.push_back({"Field Dimensions", 
                    std::to_string(video_params->field_width) + " x " + 
                    std::to_string(video_params->field_height)});
                report.items.push_back({"Total Fields", 
                    std::to_string(video_params->number_of_sequential_fields)});
                report.items.push_back({"Total Frames", 
                    std::to_string(video_params->number_of_sequential_fields / 2)});
                
                // Metrics
                report.metrics["field_count"] = static_cast<int64_t>(video_params->number_of_sequential_fields);
                report.metrics["frame_count"] = static_cast<int64_t>(video_params->number_of_sequential_fields / 2);
                report.metrics["field_width"] = static_cast<int64_t>(video_params->field_width);
                report.metrics["field_height"] = static_cast<int64_t>(video_params->field_height);
            }
        } else {
            report.items.push_back({"Status", "Error loading file"});
        }
    } catch (const std::exception& e) {
        report.items.push_back({"Status", "Error"});
        report.items.push_back({"Error", e.what()});
    }
    
    return report;
}

std::vector<PreviewOption> LDNTSCSourceStage::get_preview_options() const
{
    return PreviewHelpers::get_standard_preview_options(cached_representation_);
}

PreviewImage LDNTSCSourceStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    (void)hint;  // Unused for now
    return PreviewHelpers::render_standard_preview(cached_representation_, option_id, index);
}

} // namespace orc
