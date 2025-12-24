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
#include <stage_registry.h>
#include <stdexcept>
#include <fstream>

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
        if (!cached_representation_) {
            throw std::runtime_error("Failed to load TBC file (validation failed - see logs above)");
        }
        cached_tbc_path_ = tbc_path;
        
        // Get video parameters for logging
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
    return parameters_;
}

bool LDPALSourceStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Validate that tbc_path has correct type if present
    auto tbc_path_it = params.find("tbc_path");
    if (tbc_path_it != params.end() && !std::holds_alternative<std::string>(tbc_path_it->second)) {
        return false;
    }
    
    parameters_ = params;
    return true;
}

std::optional<StageReport> LDPALSourceStage::generate_report() const {
    StageReport report;
    report.summary = "PAL Source Status";
    
    // Get tbc_path from parameters
    std::string tbc_path;
    auto tbc_path_it = parameters_.find("tbc_path");
    if (tbc_path_it != parameters_.end()) {
        tbc_path = std::get<std::string>(tbc_path_it->second);
    }
    
    if (tbc_path.empty()) {
        report.items.push_back({"Source File", "Not configured"});
        report.items.push_back({"Status", "No TBC file path set"});
        return report;
    }
    
    report.items.push_back({"Source File", tbc_path});
    
    // Get db_path
    std::string db_path;
    auto db_path_it = parameters_.find("db_path");
    if (db_path_it != parameters_.end()) {
        db_path = std::get<std::string>(db_path_it->second);
    } else {
        db_path = tbc_path + ".db";
    }
    
    // Try to load the file to get actual information
    try {
        auto representation = create_tbc_representation(tbc_path, db_path);
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
    double dar_correction = 0.7;  // Standard PAL/NTSC DAR correction
    
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

PreviewImage LDPALSourceStage::render_preview(const std::string& option_id, uint64_t index) const
{
    PreviewImage result;
    
    if (!cached_representation_) {
        ORC_LOG_WARN("LDPALSource: No TBC loaded for preview");
        return result;  // Invalid
    }
    
    if (option_id == "field" || option_id == "field_raw") {
        // Render single field as grayscale
        bool apply_ire_scaling = (option_id == "field");
        FieldID field_id(index);
        
        if (!cached_representation_->has_field(field_id)) {
            ORC_LOG_WARN("LDPALSource: Field {} not available", index);
            return result;
        }
        
        auto descriptor = cached_representation_->get_descriptor(field_id);
        if (!descriptor) {
            ORC_LOG_WARN("LDPALSource: No descriptor for field {}", index);
            return result;
        }
        
        // Get video parameters for IRE scaling
        auto video_params = cached_representation_->get_video_parameters();
        if (!video_params) {
            return result;
        }
        
        result.width = descriptor->width;
        result.height = descriptor->height;
        result.rgb_data.resize(result.width * result.height * 3);
        
        // Get IRE levels for scaling (only used if apply_ire_scaling is true)
        double blackIRE = video_params->black_16b_ire;
        double whiteIRE = video_params->white_16b_ire;
        double ireRange = whiteIRE - blackIRE;
        const double ire_scale = 255.0 / ireRange;
        const double raw_scale = 255.0 / 65535.0;
        
        // Convert field to RGB (as grayscale from luma)
        for (uint32_t y = 0; y < descriptor->height; ++y) {
            const uint16_t* line = cached_representation_->get_line(field_id, y);
            if (!line) {
                continue;
            }
            
            for (uint32_t x = 0; x < descriptor->width; ++x) {
                // Scale 16-bit luma to 8-bit
                double sample = static_cast<double>(line[x]);
                uint8_t gray;
                
                if (apply_ire_scaling) {
                    // Apply IRE scaling (black/white levels)
                    double scaled = std::max(0.0, std::min(255.0, (sample - blackIRE) * ire_scale));
                    gray = static_cast<uint8_t>(scaled);
                } else {
                    // Raw 16-bit to 8-bit conversion (simple division)
                    gray = static_cast<uint8_t>(sample * raw_scale);
                }
                
                size_t offset = (y * descriptor->width + x) * 3;
                result.rgb_data[offset + 0] = gray;
                result.rgb_data[offset + 1] = gray;
                result.rgb_data[offset + 2] = gray;
            }
        }
        
        return result;
        
    } else if (option_id == "frame" || option_id == "frame_raw") {
        // Render frame by weaving two fields
        bool apply_ire_scaling = (option_id == "frame");
        
        // Determine field indices based on parity
        FieldID first_field(index * 2);
        FieldID second_field(index * 2 + 1);
        
        // Check parity of field 0 to adjust frame start
        auto parity_hint = cached_representation_->get_field_parity_hint(FieldID(0));
        if (parity_hint.has_value() && !parity_hint->is_first_field) {
            // Field 0 is second field, adjust indices
            first_field = FieldID(index * 2 + 1);
            second_field = FieldID(index * 2 + 2);
        }
        
        if (!cached_representation_->has_field(first_field) || 
            !cached_representation_->has_field(second_field)) {
            ORC_LOG_WARN("LDPALSource: Frame {} fields not available", index);
            return result;
        }
        
        auto desc_first = cached_representation_->get_descriptor(first_field);
        auto desc_second = cached_representation_->get_descriptor(second_field);
        if (!desc_first || !desc_second) {
            return result;
        }
        
        // Get video parameters for IRE scaling
        auto video_params = cached_representation_->get_video_parameters();
        if (!video_params) {
            return result;
        }
        
        result.width = desc_first->width;
        result.height = desc_first->height + desc_second->height;
        result.rgb_data.resize(result.width * result.height * 3);
        
        // Get IRE levels for scaling (only used if apply_ire_scaling is true)
        double blackIRE = video_params->black_16b_ire;
        double whiteIRE = video_params->white_16b_ire;
        double ireRange = whiteIRE - blackIRE;
        const double ire_scale = 255.0 / ireRange;
        const double raw_scale = 255.0 / 65535.0;
        
        // Determine field weaving order from parity hints
        // For proper interlaced display:
        // - Top field (first temporally) should go on even lines (0, 2, 4...)
        // - Bottom field (second temporally) should go on odd lines (1, 3, 5...)
        // 
        // The is_first_field hint tells us temporal order, and we map that to spatial order
        auto first_parity = cached_representation_->get_field_parity_hint(first_field);
        auto second_parity = cached_representation_->get_field_parity_hint(second_field);
        
        bool first_field_on_even_lines = true;  // Default: first field on even lines
        
        // If we have parity information, verify it matches our expectation
        // In both PAL and NTSC, the temporally first field is the top field (even lines)
        // BUT: Some sources might have inverted field order, so check both fields
        if (first_parity.has_value() && second_parity.has_value()) {
            // If first_field has is_first_field=true and second has false, keep default (true)
            // If first_field has is_first_field=false and second has true, swap (false)
            if (!first_parity->is_first_field && second_parity->is_first_field) {
                // Fields are inverted - swap the spatial order
                first_field_on_even_lines = false;
            }
        }
        
        // Weave fields - interleave lines based on parity
        for (uint32_t y = 0; y < desc_first->height; ++y) {
            const uint16_t* line_first = cached_representation_->get_line(first_field, y);
            if (line_first) {
                // Place first field on appropriate lines based on parity
                uint32_t frame_y = first_field_on_even_lines ? (y * 2) : (y * 2 + 1);
                for (uint32_t x = 0; x < result.width; ++x) {
                    double sample = static_cast<double>(line_first[x]);
                    uint8_t gray;
                    
                    if (apply_ire_scaling) {
                        // Apply IRE scaling (black/white levels)
                        double scaled = std::max(0.0, std::min(255.0, (sample - blackIRE) * ire_scale));
                        gray = static_cast<uint8_t>(scaled);
                    } else {
                        // Raw 16-bit to 8-bit conversion
                        gray = static_cast<uint8_t>(sample * raw_scale);
                    }
                    
                    size_t offset = (frame_y * result.width + x) * 3;
                    result.rgb_data[offset + 0] = gray;
                    result.rgb_data[offset + 1] = gray;
                    result.rgb_data[offset + 2] = gray;
                }
            }
        }
        
        for (uint32_t y = 0; y < desc_second->height; ++y) {
            const uint16_t* line_second = cached_representation_->get_line(second_field, y);
            if (line_second) {
                // Place second field on opposite lines from first field
                uint32_t frame_y = first_field_on_even_lines ? (y * 2 + 1) : (y * 2);
                for (uint32_t x = 0; x < result.width; ++x) {
                    double sample = static_cast<double>(line_second[x]);
                    uint8_t gray;
                    
                    if (apply_ire_scaling) {
                        // Apply IRE scaling (black/white levels)
                        double scaled = std::max(0.0, std::min(255.0, (sample - blackIRE) * ire_scale));
                        gray = static_cast<uint8_t>(scaled);
                    } else {
                        // Raw 16-bit to 8-bit conversion
                        gray = static_cast<uint8_t>(sample * raw_scale);
                    }
                    
                    size_t offset = (frame_y * result.width + x) * 3;
                    result.rgb_data[offset + 0] = gray;
                    result.rgb_data[offset + 1] = gray;
                    result.rgb_data[offset + 2] = gray;
                }
            }
        }
        
        return result;
    }
    
    // Handle split field rendering (both fields stacked vertically)
    if (option_id == "split" || option_id == "split_raw") {
        bool apply_ire_scaling = (option_id == "split");
        
        // For split view, index represents field PAIRS
        FieldID first_field(index * 2);
        FieldID second_field(index * 2 + 1);
        
        if (!cached_representation_->has_field(first_field) || 
            !cached_representation_->has_field(second_field)) {
            ORC_LOG_WARN("LDPALSource: Split fields {} not available", index);
            return result;
        }
        
        auto desc_first = cached_representation_->get_descriptor(first_field);
        auto desc_second = cached_representation_->get_descriptor(second_field);
        if (!desc_first || !desc_second) {
            return result;
        }
        
        // Get video parameters for IRE scaling
        auto video_params = cached_representation_->get_video_parameters();
        if (!video_params) {
            return result;
        }
        
        result.width = desc_first->width;
        result.height = desc_first->height + desc_second->height;  // Stack vertically
        result.rgb_data.resize(result.width * result.height * 3);
        
        // Get IRE levels for scaling
        double blackIRE = video_params->black_16b_ire;
        double whiteIRE = video_params->white_16b_ire;
        double ireRange = whiteIRE - blackIRE;
        const double ire_scale = 255.0 / ireRange;
        const double raw_scale = 255.0 / 65535.0;
        
        // Render first field in top half
        for (uint32_t y = 0; y < desc_first->height; ++y) {
            const uint16_t* line = cached_representation_->get_line(first_field, y);
            if (!line) continue;
            
            for (uint32_t x = 0; x < desc_first->width; ++x) {
                double sample = static_cast<double>(line[x]);
                uint8_t gray;
                
                if (apply_ire_scaling) {
                    double scaled = std::max(0.0, std::min(255.0, (sample - blackIRE) * ire_scale));
                    gray = static_cast<uint8_t>(scaled);
                } else {
                    gray = static_cast<uint8_t>(sample * raw_scale);
                }
                
                size_t offset = (y * result.width + x) * 3;
                result.rgb_data[offset + 0] = gray;
                result.rgb_data[offset + 1] = gray;
                result.rgb_data[offset + 2] = gray;
            }
        }
        
        // Render second field in bottom half
        for (uint32_t y = 0; y < desc_second->height; ++y) {
            const uint16_t* line = cached_representation_->get_line(second_field, y);
            if (!line) continue;
            
            for (uint32_t x = 0; x < desc_second->width; ++x) {
                double sample = static_cast<double>(line[x]);
                uint8_t gray;
                
                if (apply_ire_scaling) {
                    double scaled = std::max(0.0, std::min(255.0, (sample - blackIRE) * ire_scale));
                    gray = static_cast<uint8_t>(scaled);
                } else {
                    gray = static_cast<uint8_t>(sample * raw_scale);
                }
                
                size_t offset = ((y + desc_first->height) * result.width + x) * 3;
                result.rgb_data[offset + 0] = gray;
                result.rgb_data[offset + 1] = gray;
                result.rgb_data[offset + 2] = gray;
            }
        }
        
        return result;
    }
    
    ORC_LOG_WARN("LDPALSource: Unknown preview option '{}'", option_id);
    return result;  // Invalid
}

} // namespace orc
