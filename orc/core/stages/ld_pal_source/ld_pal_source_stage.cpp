/*
 * File:        ld_pal_source_stage.cpp
 * Module:      orc-core
 * Purpose:     LaserDisc PAL source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "ld_pal_source_stage.h"
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
ORC_REGISTER_STAGE(LDPALSourceStage)

// Force linker to include this object file
void force_link_LDPALSourceStage() {}

std::vector<ArtifactPtr> LDPALSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters
) {
    // Source stage should have no inputs
    if (!inputs.empty()) {
        throw std::runtime_error("LDPALSource stage should have no inputs");
    }

    // Get input_path parameter
    auto input_path_it = parameters.find("input_path");
    if (input_path_it == parameters.end() || std::get<std::string>(input_path_it->second).empty()) {
        // No file path configured - return empty artifact (0 fields)
        // This allows the node to exist in the DAG without a file, acting as a placeholder
        ORC_LOG_DEBUG("LDPALSource: No input_path configured, returning empty output");
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
    
    // Get optional PCM audio path
    std::string pcm_path;
    auto pcm_path_it = parameters.find("pcm_path");
    if (pcm_path_it != parameters.end()) {
        pcm_path = std::get<std::string>(pcm_path_it->second);
    }

    // Check cache
    if (cached_representation_ && cached_input_path_ == input_path) {
        ORC_LOG_DEBUG("LDPALSource: Using cached representation for {}", input_path);
        return {cached_representation_};
    }

    // Load the TBC file
    ORC_LOG_INFO("LDPALSource: Loading TBC file: {}", input_path);
    ORC_LOG_DEBUG("  Database: {}", db_path);
    if (!pcm_path.empty()) {
        ORC_LOG_DEBUG("  PCM Audio: {}", pcm_path);
    }
    
    try {
        auto tbc_representation = create_tbc_representation(input_path, db_path, pcm_path);
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
        if (video_params->system != VideoSystem::PAL && 
            video_params->system != VideoSystem::PAL_M) {
            throw std::runtime_error(
                "TBC file is not PAL format. Use 'Add LD NTSC Source' for NTSC files."
            );
        }
        
        // Run observers on all fields to extract VBI and other metadata
        ORC_LOG_INFO("LDPALSource: Running observers on all fields...");
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
        
        ORC_LOG_INFO("LDPALSource: Extracted observations for {} fields", observations_map.size());
        
        // Wrap the representation with observations
        cached_representation_ = std::make_shared<ObservationWrapperRepresentation>(
            tbc_representation, 
            observations_map
        );
        cached_input_path_ = input_path;
        
        return {cached_representation_};
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to load PAL TBC file '") + input_path + "': " + e.what()
        );
    }
}

std::vector<ParameterDescriptor> LDPALSourceStage::get_parameter_descriptors(VideoSystem project_format) const
{
    (void)project_format;  // Unused - source stages don't need project format
    std::vector<ParameterDescriptor> descriptors;
    
    // input_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "input_path";
        desc.display_name = "TBC File Path";
        desc.description = "Path to the PAL .tbc file from ld-decode (database file is automatically located)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = false;  // Optional - source provides 0 fields until path is set
        desc.file_extension_hint = ".tbc";
        descriptors.push_back(desc);
    }
    
    // pcm_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "pcm_path";
        desc.display_name = "PCM Audio File Path";
        desc.description = "Path to the analogue audio .pcm file (raw 16-bit stereo PCM at 44.1kHz)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = false;  // Optional
        desc.file_extension_hint = ".pcm";
        descriptors.push_back(desc);
    }
    
    return descriptors;
}

std::map<std::string, ParameterValue> LDPALSourceStage::get_parameters() const
{
    return parameters_;
}

bool LDPALSourceStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Validate that input_path has correct type if present
    auto input_path_it = params.find("input_path");
    if (input_path_it != params.end() && !std::holds_alternative<std::string>(input_path_it->second)) {
        return false;
    }
    
    parameters_ = params;
    return true;
}

std::optional<StageReport> LDPALSourceStage::generate_report() const {
    StageReport report;
    report.summary = "PAL Source Status";
    
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
    
    // Get optional PCM audio path
    std::string pcm_path;
    auto pcm_path_it = parameters_.find("pcm_path");
    if (pcm_path_it != parameters_.end()) {
        pcm_path = std::get<std::string>(pcm_path_it->second);
    }
    
    // Try to load the file to get actual information
    try {
        auto representation = create_tbc_representation(input_path, db_path, pcm_path);
        if (representation) {
            auto video_params = representation->get_video_parameters();
            
            report.items.push_back({"Status", "File accessible"});
            
            if (video_params) {
                report.items.push_back({"Decoder", video_params->decoder});
                
                std::string system_str;
                switch (video_params->system) {
                    case VideoSystem::PAL: system_str = "PAL"; break;
                    case VideoSystem::PAL_M: system_str = "PAL-M"; break;
                    default: system_str = "Unknown"; break;
                }
                report.items.push_back({"Video System", system_str});
                
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

bool LDPALSourceStage::supports_preview() const
{
    // Preview is available if we have a loaded TBC
    return cached_representation_ != nullptr;
}

std::vector<PreviewOption> LDPALSourceStage::get_preview_options() const
{
    std::vector<PreviewOption> options;
    
    if (!cached_representation_) {
        return options;  // No TBC loaded, no preview
    }
    
    // Get video parameters
    auto video_params = cached_representation_->get_video_parameters();
    if (!video_params) {
        return options;
    }
    
    uint64_t field_count = cached_representation_->field_count();
    if (field_count == 0) {
        return options;
    }
    
    uint32_t width = video_params->field_width;
    uint32_t height = video_params->field_height;
    
    // Calculate DAR correction based on active video region (same as PreviewHelpers)
    double dar_correction = 0.7;  // Default fallback
    if (video_params->active_video_start >= 0 && video_params->active_video_end > video_params->active_video_start &&
        video_params->first_active_frame_line >= 0 && video_params->last_active_frame_line > video_params->first_active_frame_line) {
        uint32_t active_width = video_params->active_video_end - video_params->active_video_start;
        uint32_t active_height = video_params->last_active_frame_line - video_params->first_active_frame_line;
        double active_ratio = static_cast<double>(active_width) / static_cast<double>(active_height);
        double target_ratio = 4.0 / 3.0;
        dar_correction = target_ratio / active_ratio;
    }
    
    // Option 1: Individual fields (Y component with IRE scaling)
    options.push_back(PreviewOption{
        "field",                // id
        "Field (Y)",            // display_name
        false,                  // is_rgb (this is luma/YUV data)
        width,                  // width
        height,                 // height
        field_count,            // count
        dar_correction          // dar_aspect_correction
    });
    
    // Option 2: Individual fields (raw 16-bit samples, no IRE scaling)
    options.push_back(PreviewOption{
        "field_raw",            // id
        "Field (Raw)",          // display_name
        false,                  // is_rgb
        width,                  // width
        height,                 // height
        field_count,            // count
        dar_correction          // dar_aspect_correction
    });
    
    // Option 3: Split fields (both fields stacked vertically, with IRE scaling)
    if (field_count >= 2) {
        uint64_t pair_count = field_count / 2;
        
        options.push_back(PreviewOption{
            "split",            // id
            "Split (Y)",        // display_name
            false,              // is_rgb
            width,              // width
            height * 2,         // height (two fields stacked)
            pair_count,         // count (number of field pairs)
            dar_correction      // dar_aspect_correction
        });
        
        options.push_back(PreviewOption{
            "split_raw",        // id
            "Split (Raw)",      // display_name
            false,              // is_rgb
            width,              // width
            height * 2,         // height (two fields stacked)
            pair_count,         // count (number of field pairs)
            dar_correction      // dar_aspect_correction
        });
    }
    
    // Option 4: Frames (if we have at least 2 fields)
    if (field_count >= 2) {
        uint64_t frame_count = field_count / 2;
        
        options.push_back(PreviewOption{
            "frame",            // id
            "Frame (Y)",        // display_name
            false,              // is_rgb
            width,              // width
            height * 2,         // height (two fields woven)
            frame_count,        // count
            dar_correction      // dar_aspect_correction
        });
        
        options.push_back(PreviewOption{
            "frame_raw",        // id
            "Frame (Raw)",      // display_name
            false,              // is_rgb
            width,              // width
            height * 2,         // height (two fields woven)
            frame_count,        // count
            dar_correction      // dar_aspect_correction
        });
    }
    
    return options;
}

PreviewImage LDPALSourceStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    (void)hint;  // Unused for now
    return PreviewHelpers::render_standard_preview(cached_representation_, option_id, index);
}

} // namespace orc
