/*
 * File:        ld_ntsc_source_stage.cpp
 * Module:      orc-core
 * Purpose:     LaserDisc NTSC source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "ld_ntsc_source_stage.h"
#include "logging.h"
#include "preview_renderer.h"
#include "preview_helpers.h"
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
    
    // Get optional PCM audio path
    std::string pcm_path;
    auto pcm_path_it = parameters.find("pcm_path");
    if (pcm_path_it != parameters.end()) {
        pcm_path = std::get<std::string>(pcm_path_it->second);
    }
    
    // Get optional EFM data path
    std::string efm_path;
    auto efm_path_it = parameters.find("efm_path");
    if (efm_path_it != parameters.end()) {
        efm_path = std::get<std::string>(efm_path_it->second);
    }

    // Check cache
    if (cached_representation_ && cached_input_path_ == input_path) {
        ORC_LOG_DEBUG("LDNTSCSource: Using cached representation for {}", input_path);
        return {cached_representation_};
    }

    // Load the TBC file
    ORC_LOG_INFO("LDNTSCSource: Loading TBC file: {}", input_path);
    ORC_LOG_DEBUG("  Database: {}", db_path);
    if (!pcm_path.empty()) {
        ORC_LOG_DEBUG("  PCM Audio: {}", pcm_path);
    }
    if (!efm_path.empty()) {
        ORC_LOG_DEBUG("  EFM Data: {}", efm_path);
    }
    
    try {
        auto tbc_representation = create_tbc_representation(input_path, db_path, pcm_path, efm_path);
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
        ORC_LOG_DEBUG("  Decoder: {}", video_params->decoder);
        ORC_LOG_DEBUG("  System: {}", system_str);
        ORC_LOG_DEBUG("  Fields: {} ({}x{} pixels)", 
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
        
        // Cache the representation (observations will be generated lazily per-field during rendering)
        cached_representation_ = tbc_representation;
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
        desc.constraints.default_value = std::string("");
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
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".pcm";
        descriptors.push_back(desc);
    }
    
    // efm_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "efm_path";
        desc.display_name = "EFM Data File Path";
        desc.description = "Path to the EFM data .efm file (8-bit t-values from 3-11)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = false;  // Optional
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".efm";
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
    
    // Get optional PCM audio path
    std::string pcm_path;
    auto pcm_path_it = parameters_.find("pcm_path");
    if (pcm_path_it != parameters_.end()) {
        pcm_path = std::get<std::string>(pcm_path_it->second);
    }
    
    // Get optional EFM data path
    std::string efm_path;
    auto efm_path_it = parameters_.find("efm_path");
    if (efm_path_it != parameters_.end()) {
        efm_path = std::get<std::string>(efm_path_it->second);
    }
    
    // Display PCM file path if configured
    if (!pcm_path.empty()) {
        report.items.push_back({"PCM Audio File", pcm_path});
    } else {
        report.items.push_back({"PCM Audio File", "Not configured"});
    }
    
    // Display EFM file path if configured
    if (!efm_path.empty()) {
        report.items.push_back({"EFM Data File", efm_path});
    } else {
        report.items.push_back({"EFM Data File", "Not configured"});
    }
    
    // Try to load the file to get actual information
    try {
        auto representation = create_tbc_representation(input_path, db_path, pcm_path, efm_path);
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
                
                // Calculate total audio samples and EFM t-values from metadata
                uint64_t total_audio_samples = 0;
                uint64_t total_efm_tvalues = 0;
                auto field_range = representation->field_range();
                
                for (FieldID fid = field_range.start; fid < field_range.end; ++fid) {
                    total_audio_samples += representation->get_audio_sample_count(fid);
                    total_efm_tvalues += representation->get_efm_sample_count(fid);
                }
                
                // Display audio information
                if (representation->has_audio() && total_audio_samples > 0) {
                    report.items.push_back({"Audio Samples", std::to_string(total_audio_samples)});
                    // Calculate approximate duration (44.1kHz stereo)
                    double duration_seconds = static_cast<double>(total_audio_samples) / 44100.0;
                    int minutes = static_cast<int>(duration_seconds / 60.0);
                    int seconds = static_cast<int>(duration_seconds) % 60;
                    report.items.push_back({"Audio Duration", 
                        std::to_string(minutes) + "m " + std::to_string(seconds) + "s"});
                } else {
                    report.items.push_back({"Audio Samples", "0 (no audio)"});
                }
                
                // Display EFM information
                if (representation->has_efm() && total_efm_tvalues > 0) {
                    report.items.push_back({"EFM T-Values", std::to_string(total_efm_tvalues)});
                } else {
                    report.items.push_back({"EFM T-Values", "0 (no EFM)"});
                }
                
                // Metrics
                report.metrics["field_count"] = static_cast<int64_t>(video_params->number_of_sequential_fields);
                report.metrics["frame_count"] = static_cast<int64_t>(video_params->number_of_sequential_fields / 2);
                report.metrics["field_width"] = static_cast<int64_t>(video_params->field_width);
                report.metrics["field_height"] = static_cast<int64_t>(video_params->field_height);
                report.metrics["audio_samples"] = static_cast<int64_t>(total_audio_samples);
                report.metrics["efm_tvalues"] = static_cast<int64_t>(total_efm_tvalues);
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
