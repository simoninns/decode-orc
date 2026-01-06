/*
 * File:        ffmpeg_video_sink_stage.h
 * Module:      orc-core
 * Purpose:     FFmpeg video sink stage (MP4/MKV output with audio/subtitles)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_FFMPEG_VIDEO_SINK_STAGE_H
#define ORC_CORE_FFMPEG_VIDEO_SINK_STAGE_H

#include "chroma_sink_stage.h"

namespace orc {

/**
 * @brief FFmpeg Video Sink Stage
 * 
 * Specialized video sink for FFmpeg-encoded output formats (MP4, MKV).
 * Uses the same chroma decoder as Raw Video Sink but outputs compressed video
 * and supports embedding audio, subtitles, and metadata.
 * 
 * Supported Formats:
 * - mp4-h264: H.264/AVC encoding in MP4 container
 * - mkv-ffv1: FFV1 lossless encoding in MKV container
 * 
 * Supported Decoders:
 * - PAL: pal2d, transform2d, transform3d
 * - NTSC: ntsc1d, ntsc2d, ntsc3d, ntsc3dnoadapt
 * - Other: mono, auto
 * 
 * FFmpeg-specific Features:
 * - Embed analogue audio from source (if available)
 * - Embed closed captions as mov_text subtitles (MP4 only, converts EIA-608)
 * - Encoder quality control (preset, CRF, bitrate)
 * - Multiple container formats and codecs
 * 
 * Parameters:
 * - output_path: Output file path (.mp4, .mkv)
 * - decoder_type: Which decoder to use (auto, pal2d, ntsc2d, etc.)
 * - output_format: Output format (mp4-h264, mkv-ffv1)
 * - chroma_gain: Chroma gain factor (0.0-10.0, default 1.0)
 * - chroma_phase: Chroma phase rotation in degrees (-180 to 180, default 0)
 * - luma_nr: Luma noise reduction level
 * - chroma_nr: Chroma noise reduction level
 * - ntsc_phase_comp: NTSC phase compensation (NTSC only)
 * - simple_pal: Simple PAL mode (PAL only)
 * - threads: Number of worker threads (default: auto)
 * - output_padding: Padding for codec requirements (default: 8)
 * - encoder_preset: Encoder speed/quality preset (fast, medium, slow, veryslow)
 * - encoder_crf: Constant Rate Factor for quality (0-51, lower=better, default 18)
 * - encoder_bitrate: Bitrate in bits/sec (0 = use CRF, default 0)
 * - embed_audio: Embed analogue audio in output (requires audio in source)
 * - embed_closed_captions: Embed closed captions as mov_text (MP4 only)
 */
class FFmpegVideoSinkStage : public ChromaSinkStage {
public:
    FFmpegVideoSinkStage();
    ~FFmpegVideoSinkStage() override = default;
    
    // Override node type info to provide specific identity
    NodeTypeInfo get_node_type_info() const override;
    
    // Override parameter descriptors to exclude raw format parameters
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown) const override;
    
    // Override get_parameters to include only FFmpeg-relevant parameters
    std::map<std::string, ParameterValue> get_parameters() const override;
    
    // Override set_parameters to restrict output format to FFmpeg formats only
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
};

} // namespace orc

// Force linker to include this object file
namespace orc { void force_link_FFmpegVideoSinkStage(); }

#endif // ORC_CORE_FFMPEG_VIDEO_SINK_STAGE_H
