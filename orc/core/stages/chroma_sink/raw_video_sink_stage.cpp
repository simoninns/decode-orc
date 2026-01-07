/*
 * File:        raw_video_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Raw video sink stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "raw_video_sink_stage.h"
#include "stage_registry.h"
#include "logging.h"

namespace orc {

// Register stage with registry
ORC_REGISTER_STAGE(RawVideoSinkStage)

// Force linker to include this object file
void force_link_RawVideoSinkStage() {}

RawVideoSinkStage::RawVideoSinkStage() : ChromaSinkStage()
{
}

NodeTypeInfo RawVideoSinkStage::get_node_type_info() const
{
    return NodeTypeInfo{
        NodeType::SINK,
        "raw_video_sink",
        "Raw Video Sink",
        "Decodes composite video to raw RGB/YUV/Y4M files. Uses the same chroma decoders as FFmpeg Video Sink but outputs uncompressed raw data. Trigger to export.",
        1,  // min_inputs
        1,  // max_inputs
        0,  // min_outputs
        0,  // max_outputs
        VideoFormatCompatibility::ALL
    };
}

std::vector<ParameterDescriptor> RawVideoSinkStage::get_parameter_descriptors(VideoSystem project_format) const
{
    // Get base parameters from ChromaSinkStage
    auto params = ChromaSinkStage::get_parameter_descriptors(project_format);
    
    // Filter out FFmpeg-specific parameters and modify output_format
    std::vector<ParameterDescriptor> filtered_params;
    
    for (const auto& param : params) {
        // Skip FFmpeg-specific parameters
        if (param.name == "encoder_preset" ||
            param.name == "encoder_crf" ||
            param.name == "encoder_bitrate" ||
            param.name == "embed_audio" ||
            param.name == "embed_closed_captions") {
            continue;
        }
        
        // Modify output_format to show only raw formats
        if (param.name == "output_format") {
            ParameterDescriptor modified_param = param;
            modified_param.description = "Output format:\n"
                                         "  rgb  - RGB48 (16-bit per channel, planar)\n"
                                         "  yuv  - YUV444P16 (16-bit per channel, planar)\n"
                                         "  y4m  - YUV444P16 with Y4M headers";
            // Override options to only include raw formats
            modified_param.constraints.allowed_strings = {"rgb", "yuv", "y4m"};
            filtered_params.push_back(modified_param);
            continue;
        }
        
        // Modify file extension hint for output_path
        if (param.name == "output_path") {
            ParameterDescriptor modified_param = param;
            modified_param.file_extension_hint = ".rgb|.yuv|.y4m";
            filtered_params.push_back(modified_param);
            continue;
        }
        
        // Keep all other parameters
        filtered_params.push_back(param);
    }
    
    return filtered_params;
}

std::map<std::string, ParameterValue> RawVideoSinkStage::get_parameters() const
{
    // Get base parameters
    auto params = ChromaSinkStage::get_parameters();
    
    // Remove FFmpeg-specific parameters
    params.erase("encoder_preset");
    params.erase("encoder_crf");
    params.erase("encoder_bitrate");
    params.erase("embed_audio");
    params.erase("embed_closed_captions");
    
    return params;
}

bool RawVideoSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Validate output_format is raw format only
    auto it = params.find("output_format");
    if (it != params.end()) {
        if (std::holds_alternative<std::string>(it->second)) {
            std::string format = std::get<std::string>(it->second);
            if (format != "rgb" && format != "yuv" && format != "y4m") {
                ORC_LOG_ERROR("RawVideoSink: Invalid output format '{}' - must be rgb, yuv, or y4m", format);
                return false;
            }
        }
    }
    
    // Create a filtered parameter map without FFmpeg-specific parameters
    std::map<std::string, ParameterValue> filtered_params = params;
    filtered_params.erase("encoder_preset");
    filtered_params.erase("encoder_crf");
    filtered_params.erase("encoder_bitrate");
    filtered_params.erase("embed_audio");
    filtered_params.erase("embed_closed_captions");
    
    // Call base class implementation
    return ChromaSinkStage::set_parameters(filtered_params);
}

} // namespace orc
