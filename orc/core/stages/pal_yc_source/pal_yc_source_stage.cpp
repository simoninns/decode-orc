/*
 * File:        pal_yc_source_stage.cpp
 * Module:      orc-core
 * Purpose:     PAL YC source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "pal_yc_source_stage.h"
#include "logging.h"
#include "preview_renderer.h"
#include "preview_helpers.h"
#include <stage_registry.h>
#include <stdexcept>
#include <fstream>

namespace orc {

// Register this stage with the registry
ORC_REGISTER_STAGE(PALYCSourceStage)

// Force linker to include this object file
void force_link_PALYCSourceStage() {}

std::vector<ArtifactPtr> PALYCSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters
) {
    // Source stage should have no inputs
    if (!inputs.empty()) {
        throw std::runtime_error("PAL_YC_Source stage should have no inputs");
    }

    // Get y_path parameter
    auto y_path_it = parameters.find("y_path");
    if (y_path_it == parameters.end() || std::get<std::string>(y_path_it->second).empty()) {
        // No file path configured - return empty artifact (0 fields)
        ORC_LOG_DEBUG("PAL_YC_Source: No y_path configured, returning empty output");
        return {};
    }
    std::string y_path = std::get<std::string>(y_path_it->second);

    // Get c_path parameter
    auto c_path_it = parameters.find("c_path");
    if (c_path_it == parameters.end() || std::get<std::string>(c_path_it->second).empty()) {
        // No C path configured
        ORC_LOG_DEBUG("PAL_YC_Source: No c_path configured, returning empty output");
        return {};
    }
    std::string c_path = std::get<std::string>(c_path_it->second);

    // Get db_path parameter
    auto db_path_it = parameters.find("db_path");
    std::string db_path;
    if (db_path_it != parameters.end() && !std::get<std::string>(db_path_it->second).empty()) {
        db_path = std::get<std::string>(db_path_it->second);
    } else {
        // Default: y_path + ".db"
        db_path = y_path + ".db";
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
    if (cached_representation_ && cached_y_path_ == y_path && cached_c_path_ == c_path) {
        ORC_LOG_DEBUG("PAL_YC_Source: Using cached representation for {} + {}", y_path, c_path);
        return {cached_representation_};
    }

    // Load the YC files
    ORC_LOG_INFO("PAL_YC_Source: Loading YC files");
    ORC_LOG_DEBUG("  Y file: {}", y_path);
    ORC_LOG_DEBUG("  C file: {}", c_path);
    ORC_LOG_DEBUG("  Database: {}", db_path);
    if (!pcm_path.empty()) {
        ORC_LOG_DEBUG("  PCM Audio: {}", pcm_path);
    }
    if (!efm_path.empty()) {
        ORC_LOG_DEBUG("  EFM Data: {}", efm_path);
    }
    
    try {
        auto yc_representation = create_tbc_yc_representation(y_path, c_path, db_path, pcm_path, efm_path);
        if (!yc_representation) {
            throw std::runtime_error("Failed to load YC files (validation failed - see logs above)");
        }
        
        // Get video parameters for logging
        auto video_params = yc_representation->get_video_parameters();
        if (!video_params) {
            throw std::runtime_error("No video parameters found in YC metadata");
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
        
        // Check system
        if (video_params->system != VideoSystem::PAL && 
            video_params->system != VideoSystem::PAL_M) {
            throw std::runtime_error(
                "YC files are not PAL format. Use 'Add NTSC YC Source' for NTSC files."
            );
        }
        
        // Cache the representation
        cached_representation_ = yc_representation;
        cached_y_path_ = y_path;
        cached_c_path_ = c_path;
        
        return {cached_representation_};
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to load PAL YC files '") + y_path + "' + '" + c_path + "': " + e.what()
        );
    }
}

std::vector<ParameterDescriptor> PALYCSourceStage::get_parameter_descriptors(VideoSystem project_format, SourceType source_type) const
{
    (void)project_format;  // Unused - source stages don't need project format
    (void)source_type;     // Unused - source stages define the source type
    std::vector<ParameterDescriptor> descriptors;
    
    // y_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "y_path";
        desc.display_name = "Y (Luma) File Path";
        desc.description = "Path to the PAL .tbcy (luma) file";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = false;  // Optional - source provides 0 fields until path is set
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".tbcy";
        descriptors.push_back(desc);
    }
    
    // c_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "c_path";
        desc.display_name = "C (Chroma) File Path";
        desc.description = "Path to the PAL .tbcc (chroma) file";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = false;  // Optional
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".tbcc";
        descriptors.push_back(desc);
    }
    
    // db_path parameter
    {
        ParameterDescriptor desc;
        desc.name = "db_path";
        desc.display_name = "Database File Path";
        desc.description = "Path to the .tbc.db metadata file (defaults to Y file path + .db)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = false;  // Optional - defaults to y_path + ".db"
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".db";
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

std::map<std::string, ParameterValue> PALYCSourceStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["y_path"] = y_path_;
    params["c_path"] = c_path_;
    params["db_path"] = db_path_;
    params["pcm_path"] = pcm_path_;
    params["efm_path"] = efm_path_;
    return params;
}

bool PALYCSourceStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Extract and validate parameters
    auto y_path_it = params.find("y_path");
    if (y_path_it != params.end()) {
        if (!std::holds_alternative<std::string>(y_path_it->second)) {
            return false;
        }
        y_path_ = std::get<std::string>(y_path_it->second);
    }
    
    auto c_path_it = params.find("c_path");
    if (c_path_it != params.end()) {
        if (!std::holds_alternative<std::string>(c_path_it->second)) {
            return false;
        }
        c_path_ = std::get<std::string>(c_path_it->second);
    }
    
    auto db_path_it = params.find("db_path");
    if (db_path_it != params.end()) {
        if (!std::holds_alternative<std::string>(db_path_it->second)) {
            return false;
        }
        db_path_ = std::get<std::string>(db_path_it->second);
    }
    
    auto pcm_path_it = params.find("pcm_path");
    if (pcm_path_it != params.end()) {
        if (!std::holds_alternative<std::string>(pcm_path_it->second)) {
            return false;
        }
        pcm_path_ = std::get<std::string>(pcm_path_it->second);
    }
    
    auto efm_path_it = params.find("efm_path");
    if (efm_path_it != params.end()) {
        if (!std::holds_alternative<std::string>(efm_path_it->second)) {
            return false;
        }
        efm_path_ = std::get<std::string>(efm_path_it->second);
    }
    
    return true;
}

std::optional<StageReport> PALYCSourceStage::generate_report() const {
    StageReport report;
    report.summary = "PAL YC Source Status";
    
    if (y_path_.empty() || c_path_.empty()) {
        report.items.push_back({"Source Files", "Not configured"});
        report.items.push_back({"Status", "No YC file paths set"});
        return report;
    }
    
    report.items.push_back({"Y (Luma) File", y_path_});
    report.items.push_back({"C (Chroma) File", c_path_});
    
    // Get db_path
    std::string effective_db_path = db_path_.empty() ? (y_path_ + ".db") : db_path_;
    report.items.push_back({"Database File", effective_db_path});
    
    // Display PCM file path if configured
    if (!pcm_path_.empty()) {
        report.items.push_back({"PCM Audio File", pcm_path_});
    } else {
        report.items.push_back({"PCM Audio File", "Not configured"});
    }
    
    // Display EFM file path if configured
    if (!efm_path_.empty()) {
        report.items.push_back({"EFM Data File", efm_path_});
    } else {
        report.items.push_back({"EFM Data File", "Not configured"});
    }
    
    // Try to load the files to get actual information
    try {
        auto representation = create_tbc_yc_representation(y_path_, c_path_, effective_db_path, pcm_path_, efm_path_);
        if (representation) {
            auto video_params = representation->get_video_parameters();
            
            report.items.push_back({"Status", "Files accessible"});
            report.items.push_back({"Channel Mode", "YC (Separate Y and C)"});
            
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
            report.items.push_back({"Status", "Error loading files"});
        }
    } catch (const std::exception& e) {
        report.items.push_back({"Status", "Error"});
        report.items.push_back({"Error", e.what()});
    }
    
    return report;
}

bool PALYCSourceStage::supports_preview() const
{
    // Preview is available if we have a loaded YC representation
    return cached_representation_ != nullptr;
}

std::vector<PreviewOption> PALYCSourceStage::get_preview_options() const
{
    // YC sources return standard preview options.
    // The GUI will detect has_separate_channels() and provide a separate Signal dropdown (Y/C/Y+C).
    // When calling render_preview(), the GUI combines mode + channel (e.g., "field_y", "split_c").
    return PreviewHelpers::get_standard_preview_options(cached_representation_);
}

PreviewImage PALYCSourceStage::render_preview(
    const std::string& option_id, 
    uint64_t index,
    PreviewNavigationHint hint
) const {
    (void)hint;  // Unused for now
    
    if (!cached_representation_) {
        return PreviewImage{};
    }
    
    // Determine channel and base option from option_id
    // Expected formats: field_y, field_y_raw, split_c, split_c_raw, frame_yc, frame_yc_raw
    RenderChannel channel = RenderChannel::COMPOSITE;
    std::string base_option = option_id;
    
    // Parse channel suffix: _yc (must check first, before _y or _c)
    if (option_id.find("_yc") != std::string::npos) {
        channel = RenderChannel::COMPOSITE_YC;
        size_t pos = option_id.find("_yc");
        base_option = option_id.substr(0, pos);
        // Check if there's _raw after _yc
        if (pos + 3 < option_id.size() && option_id.substr(pos + 3) == "_raw") {
            base_option += "_raw";
        }
    } else if (option_id.find("_y") != std::string::npos) {
        channel = RenderChannel::LUMA_ONLY;
        size_t pos = option_id.find("_y");
        base_option = option_id.substr(0, pos);
        // Check if there's _raw after _y
        if (pos + 2 < option_id.size() && option_id.substr(pos + 2) == "_raw") {
            base_option += "_raw";
        }
    } else if (option_id.find("_c") != std::string::npos) {
        channel = RenderChannel::CHROMA_ONLY;
        size_t pos = option_id.find("_c");
        base_option = option_id.substr(0, pos);
        // Check if there's _raw after _c
        if (pos + 2 < option_id.size() && option_id.substr(pos + 2) == "_raw") {
            base_option += "_raw";
        }
    }
    
    // Render using the appropriate channel
    return PreviewHelpers::render_standard_preview_with_channel(
        cached_representation_, base_option, index, channel);
}

} // namespace orc
