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
#include <stage_registry.h>
#include <stdexcept>

namespace orc {

// Register this stage with the registry
static StageRegistration ld_pal_source_registration([]() {
    return std::make_shared<LDPALSourceStage>();
});

std::vector<ArtifactPtr> LDPALSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters
) {
    // Source stage should have no inputs
    if (!inputs.empty()) {
        throw std::runtime_error("LDPALSource stage should have no inputs");
    }

    // Get tbc_path parameter
    auto tbc_path_it = parameters.find("tbc_path");
    if (tbc_path_it == parameters.end() || std::get<std::string>(tbc_path_it->second).empty()) {
        // No file path configured - return empty artifact (0 fields)
        // This allows the node to exist in the DAG without a file, acting as a placeholder
        ORC_LOG_DEBUG("LDPALSource: No tbc_path configured, returning empty output");
        return {};
    }
    std::string tbc_path = std::get<std::string>(tbc_path_it->second);

    // Get db_path parameter (optional)
    std::string db_path;
    auto db_path_it = parameters.find("db_path");
    if (db_path_it != parameters.end()) {
        db_path = std::get<std::string>(db_path_it->second);
    } else {
        // Default: tbc_path + ".db"
        db_path = tbc_path + ".db";
    }

    // Check cache
    if (cached_representation_ && cached_tbc_path_ == tbc_path) {
        ORC_LOG_DEBUG("LDPALSource: Using cached representation for {}", tbc_path);
        return {cached_representation_};
    }

    // Load the TBC file
    ORC_LOG_INFO("LDPALSource: Loading TBC file: {}", tbc_path);
    ORC_LOG_DEBUG("  Database: {}", db_path);
    
    try {
        cached_representation_ = create_tbc_representation(tbc_path, db_path);
        cached_tbc_path_ = tbc_path;
        
        // Verify decoder and system
        auto video_params = cached_representation_->get_video_parameters();
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
        ORC_LOG_DEBUG("  Field size: {}x{}", video_params->field_width, video_params->field_height);
        
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
        
        return {cached_representation_};
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to load PAL TBC file '") + tbc_path + "': " + e.what()
        );
    }
}

std::vector<ParameterDescriptor> LDPALSourceStage::get_parameter_descriptors() const
{
    std::vector<ParameterDescriptor> descriptors;
    
    // tbc_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "tbc_path";
        desc.display_name = "TBC File Path";
        desc.description = "Path to the PAL .tbc file from ld-decode (database file is automatically located)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = false;  // Optional - source provides 0 fields until path is set
        descriptors.push_back(desc);
    }
    
    return descriptors;
}

std::map<std::string, ParameterValue> LDPALSourceStage::get_parameters() const
{
    // Source stages don't maintain parameter state - parameters are passed to execute()
    return {};
}

bool LDPALSourceStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Source stages don't maintain parameter state - parameters are passed to execute()
    // Just validate that tbc_path has correct type if present
    auto tbc_path_it = params.find("tbc_path");
    if (tbc_path_it != params.end() && !std::holds_alternative<std::string>(tbc_path_it->second)) {
        return false;
    }
    
    return true;
}

} // namespace orc
