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
#include <stage_registry.h>
#include <stdexcept>

namespace orc {

// Register this stage with the registry
static StageRegistration ld_ntsc_source_registration([]() {
    return std::make_shared<LDNTSCSourceStage>();
});

std::vector<ArtifactPtr> LDNTSCSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters
) {
    // Source stage should have no inputs
    if (!inputs.empty()) {
        throw std::runtime_error("LDNTSCSource stage should have no inputs");
    }

    // Get tbc_path parameter
    auto tbc_path_it = parameters.find("tbc_path");
    if (tbc_path_it == parameters.end()) {
        throw std::runtime_error("LDNTSCSource stage requires 'tbc_path' parameter");
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
        ORC_LOG_DEBUG("LDNTSCSource: Using cached representation for {}", tbc_path);
        return {cached_representation_};
    }

    // Load the TBC file
    ORC_LOG_INFO("LDNTSCSource: Loading TBC file: {}", tbc_path);
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
        if (video_params->system != VideoSystem::NTSC) {
            throw std::runtime_error(
                "TBC file is not NTSC format. Use 'Add LD PAL Source' for PAL files."
            );
        }
        
        return {cached_representation_};
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to load NTSC TBC file '") + tbc_path + "': " + e.what()
        );
    }
}

} // namespace orc
