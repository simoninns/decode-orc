/*
 * File:        raw_video_sink_stage.h
 * Module:      orc-core
 * Purpose:     Raw video sink stage (RGB/YUV/Y4M output)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_RAW_VIDEO_SINK_STAGE_H
#define ORC_CORE_RAW_VIDEO_SINK_STAGE_H

#include "chroma_sink_stage.h"

namespace orc {

/**
 * @brief Raw Video Sink Stage
 * 
 * Specialized video sink for raw output formats (RGB, YUV, Y4M).
 * Uses the same chroma decoder as FFmpeg Video Sink but outputs uncompressed raw files.
 * 
 * Supported Formats:
 * - rgb: RGB48 (16-bit per channel, planar)
 * - yuv: YUV444P16 (16-bit per channel, planar)
 * - y4m: YUV444P16 with Y4M headers (for compatibility with other tools)
 * 
 * Supported Decoders:
 * - PAL: pal2d, transform2d, transform3d
 * - NTSC: ntsc1d, ntsc2d, ntsc3d, ntsc3dnoadapt
 * - Other: mono, auto
 * 
 * This sink does NOT support:
 * - Audio embedding (use FFmpeg Video Sink for that)
 * - Subtitle embedding (use FFmpeg Video Sink for that)
 * - Video compression (use FFmpeg Video Sink for that)
 * 
 * Parameters:
 * - output_path: Output file path (.rgb, .yuv, or .y4m)
 * - decoder_type: Which decoder to use (auto, pal2d, ntsc2d, etc.)
 * - output_format: Output format (rgb, yuv, or y4m)
 * - chroma_gain: Chroma gain factor (0.0-10.0, default 1.0)
 * - chroma_phase: Chroma phase rotation in degrees (-180 to 180, default 0)
 * - luma_nr: Luma noise reduction level
 * - chroma_nr: Chroma noise reduction level
 * - ntsc_phase_comp: NTSC phase compensation (NTSC only)
 * - simple_pal: Simple PAL mode (PAL only)
 * - threads: Number of worker threads (default: auto)
 * - output_padding: Padding for alignment (default: 8)
 */
class RawVideoSinkStage : public ChromaSinkStage {
public:
    RawVideoSinkStage();
    ~RawVideoSinkStage() override = default;
    
    // Override node type info to provide specific identity
    NodeTypeInfo get_node_type_info() const override;
    
    // Override parameter descriptors to exclude FFmpeg-specific parameters
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown, SourceType source_type = SourceType::Unknown) const override;
    
    // Override get_parameters to exclude FFmpeg-specific parameters
    std::map<std::string, ParameterValue> get_parameters() const override;
    
    // Override set_parameters to restrict output format to raw formats only
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
};

} // namespace orc

// Force linker to include this object file
namespace orc { void force_link_RawVideoSinkStage(); }

#endif // ORC_CORE_RAW_VIDEO_SINK_STAGE_H
