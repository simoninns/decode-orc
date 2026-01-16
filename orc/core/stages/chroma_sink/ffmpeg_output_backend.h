/*
 * File:        ffmpeg_output_backend.h
 * Module:      orc-core
 * Purpose:     FFmpeg-based video encoding output backend
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_FFMPEG_OUTPUT_BACKEND_H
#define ORC_CORE_FFMPEG_OUTPUT_BACKEND_H

#include "output_backend.h"
#include "field_id.h"
#include "eia608_decoder.h"
#include <memory>

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
    
    // Audio structures
    AVCodecContext* audio_codec_ctx_ = nullptr;
    AVStream* audio_stream_ = nullptr;
    AVFrame* audio_frame_ = nullptr;
    AVPacket* audio_packet_ = nullptr;
    int64_t audio_pts_ = 0;
    const VideoFieldRepresentation* vfr_ = nullptr;
    uint64_t start_field_index_ = 0;
    uint64_t num_fields_ = 0;
    uint64_t current_field_for_audio_ = 0;
    bool embed_audio_ = false;
    std::vector<int16_t> audio_buffer_;  // Persistent buffer for audio samples across frames
    
    // Subtitle structures (for closed captions)
    AVCodecContext* subtitle_codec_ctx_ = nullptr;
    AVStream* subtitle_stream_ = nullptr;
    bool embed_closed_captions_ = false;
    uint64_t current_field_for_captions_ = 0;
    std::unique_ptr<EIA608Decoder> eia608_decoder_;
    std::vector<CaptionCue> pending_cues_;
    size_t next_cue_index_ = 0;
    
    // State
    int64_t pts_ = 0;
    int frames_written_ = 0;
    std::string codec_name_;
    std::string container_format_;
    
    // Video parameters
    int width_ = 0;           // Output dimensions (may be padded)
    int height_ = 0;
    int src_width_ = 0;       // Source ComponentFrame dimensions (before padding)
    int src_height_ = 0;
    int active_width_ = 0;    // Active video region dimensions
    int active_height_ = 0;
    AVRational time_base_;
    VideoSystem video_system_ = VideoSystem::PAL;
    double black_ire_ = 0.0;
    double white_ire_ = 0.0;
    orc::VideoParameters video_params_;
    
    // Crop parameters
    int crop_top_ = 0;
    
    // Encoder quality settings
    std::string encoder_preset_ = "medium";
    int encoder_crf_ = 18;
    int encoder_bitrate_ = 0;
    bool use_lossless_mode_ = false;
    std::string prores_profile_ = "hq";
    
    // Helper methods
    bool setupEncoder(const std::string& codec_id, const orc::VideoParameters& params);
    bool setupAudioEncoder();
    bool setupSubtitleEncoder();
    bool encodeAudioForFrame();
    bool encodeClosedCaptionsForFrame();
    bool convertAndEncode(const ComponentFrame& component_frame);
    void cleanup();
};

} // namespace orc

#endif // HAVE_FFMPEG

#endif // ORC_CORE_FFMPEG_OUTPUT_BACKEND_H
