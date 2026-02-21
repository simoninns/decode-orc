/*
 * File:        ffmpeg_video_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     FFmpeg video sink stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "ffmpeg_video_sink_stage.h"
#include "stage_registry.h"
#include "logging.h"
#include "output_backend.h"
#include "closed_caption_observer.h"
#include "observation_context.h"

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

std::vector<ParameterDescriptor> FFmpegVideoSinkStage::get_parameter_descriptors(VideoSystem project_format, SourceType source_type) const
{
    // Get base parameters from ChromaSinkStage
    auto params = ChromaSinkStage::get_parameter_descriptors(project_format, source_type);
    
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
            modified_param.description = "Output container and codec combination:\n"
                                         "Lossless/Archive:\n"
                                         "  mkv-ffv1 - FFV1 lossless codec in MKV container\n"
                                         "Professional:\n"
                                         "  mov-prores - ProRes codec (profile set via ProRes Profile parameter)\n"
                                         "Uncompressed:\n"
                                         "  mov-v210 - 10-bit 4:2:2 uncompressed\n"
                                         "  mov-v410 - 10-bit 4:4:4 uncompressed\n"
                                         "Broadcast:\n"
                                         "  mxf-mpeg2video - D10 (Sony IMX/XDCAM)\n"
                                         "H.264 (universal compatibility):\n"
                                         "  mp4-h264 - H.264 in MP4 container\n"
                                         "  mov-h264 - H.264 in MOV container\n"
                                         "H.265/HEVC (better compression):\n"
                                         "  mp4-hevc - H.265/HEVC in MP4 container\n"
                                         "  mov-hevc - H.265/HEVC in MOV container\n"
                                         "AV1 (modern, efficient):\n"
                                         "  mp4-av1 - AV1 codec in MP4 container\n"
                                         "\n"
                                         "Note: Hardware acceleration and lossless mode are set via separate parameters";
            // Override options to only include FFmpeg formats
            modified_param.constraints.allowed_strings = ffmpeg_formats;
            filtered_params.push_back(modified_param);
            continue;
        }
        
        // Modify file extension hint for output_path
        if (param.name == "output_path") {
            ParameterDescriptor modified_param = param;
            modified_param.file_extension_hint = ".mp4|.mkv|.mov|.mxf";
            modified_param.description = "Path to output video file (MP4, MKV, MOV, or MXF format)";
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

bool FFmpegVideoSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context)
{
    // Check if closed caption embedding is enabled in parameters
    bool embed_cc = false;
    auto cc_param = parameters.find("embed_closed_captions");
    if (cc_param != parameters.end()) {
        if (std::holds_alternative<bool>(cc_param->second)) {
            embed_cc = std::get<bool>(cc_param->second);
        } else if (std::holds_alternative<std::string>(cc_param->second)) {
            std::string val = std::get<std::string>(cc_param->second);
            embed_cc = (val == "true" || val == "1" || val == "yes");
        }
    }
    
    // If closed caption embedding is enabled, instantiate ClosedCaptionObserver
    // to populate observation context before calling parent trigger
    if (embed_cc) {
        ORC_LOG_DEBUG("FFmpegVideoSink: Closed caption embedding enabled, extracting CC observations");
        
        // Extract VideoFieldRepresentation from input
        if (!inputs.empty()) {
            auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
            if (vfr) {
                // Create and run ClosedCaptionObserver to populate observations
                auto cc_observer = std::make_shared<ClosedCaptionObserver>();
                
                // Get field range from VFR
                auto field_range = vfr->field_range();
                
                // Run observer on all fields to extract CC data
                 for (FieldID::value_type field_num = field_range.start.value(); 
                     field_num <= field_range.end.value(); ++field_num) {
                    FieldID field_id(field_num);
                    if (vfr->has_field(field_id)) {
                        cc_observer->process_field(*vfr, field_id, observation_context);
                    }
                }
                
                ORC_LOG_DEBUG("FFmpegVideoSink: CC observations extracted for fields {}-{}", 
                             field_range.start.value(), field_range.end.value());
            }
        }
    }
    
    // Call parent trigger which will use the populated observation context
    return ChromaSinkStage::trigger(inputs, parameters, observation_context);
}

} // namespace orc
