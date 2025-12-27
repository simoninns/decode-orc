/*
 * File:        ffmpeg_output_backend.h
 * Module:      orc-core
 * Purpose:     FFmpeg-based video encoding output backend
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_FFMPEG_OUTPUT_BACKEND_H
#define ORC_CORE_FFMPEG_OUTPUT_BACKEND_H

#include "output_backend.h"

#ifdef HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace orc {

/**
 * @brief FFmpeg-based output backend for encoded video
 * 
 * Supports H.264, H.265, FFV1, and other codecs via libav* libraries.
 */
class FFmpegOutputBackend : public OutputBackend {
public:
    FFmpegOutputBackend();
    ~FFmpegOutputBackend() override;
    
    bool initialize(const Configuration& config) override;
    bool writeFrame(const ComponentFrame& frame) override;
    bool finalize() override;
    std::string getFormatInfo() const override;
    
private:
    // FFmpeg context structures
    AVFormatContext* format_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    AVStream* stream_ = nullptr;
    AVFrame* frame_ = nullptr;          // Destination frame (encoder's pixel format)
    AVFrame* src_frame_ = nullptr;      // Source frame (YUV444P16LE from ComponentFrame)
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    
    // State
    int64_t pts_ = 0;
    int frames_written_ = 0;
    std::string codec_name_;
    std::string container_format_;
    
    // Video parameters
    int width_ = 0;
    int height_ = 0;
    int active_width_ = 0;
    int active_height_ = 0;
    AVRational time_base_;
    VideoSystem video_system_ = VideoSystem::PAL;
    double black_ire_ = 0.0;
    double white_ire_ = 0.0;
    
    // Helper methods
    bool setupEncoder(const std::string& codec_id, const orc::VideoParameters& params);
    bool convertAndEncode(const ComponentFrame& component_frame);
    void cleanup();
};

} // namespace orc

#endif // HAVE_FFMPEG

#endif // ORC_CORE_FFMPEG_OUTPUT_BACKEND_H
