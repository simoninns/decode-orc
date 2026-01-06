/*
 * File:        ffmpeg_video_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     FFmpeg video sink stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "ffmpeg_video_sink_stage.h"
#include "stage_registry.h"
#include "logging.h"
#include "output_backend.h"

namespace orc {

// Register stage with registry
ORC_REGISTER_STAGE(FFmpegVideoSinkStage)

// Force linker to include this object file
void force_link_FFmpegVideoSinkStage() {}

FFmpegVideoSinkStage::FFmpegVideoSinkStage() : ChromaSinkStage()
{
}

NodeTypeInfo FFmpegVideoSinkStage::get_node_type_info() const
{
    return NodeTypeInfo{
        NodeType::SINK,
        "ffmpeg_video_sink",
        "FFmpeg Video Sink",
        "Decodes composite video to MP4/MKV with optional audio and subtitles. Uses the same chroma decoders as Raw Video Sink but outputs compressed video files. Trigger to export.",
        1,  // min_inputs
        1,  // max_inputs
        0,  // min_outputs
        0,  // max_outputs
        VideoFormatCompatibility::ALL
    };
}

std::vector<ParameterDescriptor> FFmpegVideoSinkStage::get_parameter_descriptors(VideoSystem project_format) const
{
    // Get base parameters from ChromaSinkStage
    auto params = ChromaSinkStage::get_parameter_descriptors(project_format);
    
    // Filter to only include FFmpeg-relevant parameters and modify some
    std::vector<ParameterDescriptor> filtered_params;
    
    // Get supported FFmpeg formats
    std::vector<std::string> ffmpeg_formats;
#ifdef HAVE_FFMPEG
    auto all_formats = OutputBackendFactory::getSupportedFormats();
    for (const auto& fmt : all_formats) {
        // Include only formats that are not raw (rgb, yuv, y4m)
        if (fmt != "rgb" && fmt != "yuv" && fmt != "y4m") {
            ffmpeg_formats.push_back(fmt);
        }
    }
#endif
    
    // If no FFmpeg formats available, add placeholder
    if (ffmpeg_formats.empty()) {
        ffmpeg_formats.push_back("mp4-h264");  // Placeholder for UI
    }
    
    for (const auto& param : params) {
        // Modify output_format to show only FFmpeg formats
        if (param.name == "output_format") {
            ParameterDescriptor modified_param = param;
            modified_param.description = "Output format:\n"
                                         "  mp4-h264 - H.264/AVC encoding in MP4 container\n"
                                         "  mkv-ffv1 - FFV1 lossless encoding in MKV container";
            // Override options to only include FFmpeg formats
            modified_param.constraints.allowed_strings = ffmpeg_formats;
            filtered_params.push_back(modified_param);
            continue;
        }
        
        // Modify file extension hint for output_path
        if (param.name == "output_path") {
            ParameterDescriptor modified_param = param;
            modified_param.file_extension_hint = ".mp4|.mkv";
            modified_param.description = "Path to output video file (MP4 or MKV format)";
            filtered_params.push_back(modified_param);
            continue;
        }
        
        // Keep all parameters (including FFmpeg-specific ones)
        filtered_params.push_back(param);
    }
    
    return filtered_params;
}

std::map<std::string, ParameterValue> FFmpegVideoSinkStage::get_parameters() const
{
    // Return all base parameters (includes FFmpeg-specific ones)
    return ChromaSinkStage::get_parameters();
}

bool FFmpegVideoSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Validate output_format is FFmpeg format only
    auto it = params.find("output_format");
    if (it != params.end()) {
        if (std::holds_alternative<std::string>(it->second)) {
            std::string format = std::get<std::string>(it->second);
            // Reject raw formats
            if (format == "rgb" || format == "yuv" || format == "y4m") {
                ORC_LOG_ERROR("FFmpegVideoSink: Invalid output format '{}' - use mp4-h264 or mkv-ffv1", format);
                return false;
            }
            
            // Verify format is supported by FFmpeg backend
#ifdef HAVE_FFMPEG
            auto supported_formats = OutputBackendFactory::getSupportedFormats();
            bool is_supported = false;
            for (const auto& fmt : supported_formats) {
                if (fmt == format) {
                    is_supported = true;
                    break;
                }
            }
            if (!is_supported) {
                ORC_LOG_ERROR("FFmpegVideoSink: Output format '{}' not supported (FFmpeg not available)", format);
                return false;
            }
#else
            ORC_LOG_ERROR("FFmpegVideoSink: FFmpeg support not compiled in, cannot use format '{}'", format);
            return false;
#endif
        }
    }
    
    // Call base class implementation (it handles all parameters)
    return ChromaSinkStage::set_parameters(params);
}

} // namespace orc
