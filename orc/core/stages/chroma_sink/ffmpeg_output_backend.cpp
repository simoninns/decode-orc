/*
 * File:        ffmpeg_output_backend.cpp
 * Module:      orc-core
 * Purpose:     FFmpeg-based video encoding implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "ffmpeg_output_backend.h"

#ifdef HAVE_FFMPEG

#include "componentframe.h"
#include "logging.h"
#include <algorithm>
#include <cstring>
#include <thread>

namespace orc {

FFmpegOutputBackend::FFmpegOutputBackend()
{
}

FFmpegOutputBackend::~FFmpegOutputBackend()
{
    cleanup();
}

void FFmpegOutputBackend::cleanup()
{
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    
    if (src_frame_) {
        av_frame_free(&src_frame_);
        src_frame_ = nullptr;
    }
    
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
    
    if (format_ctx_) {
        if (format_ctx_->pb) {
            avio_closep(&format_ctx_->pb);
        }
        avformat_free_context(format_ctx_);
        format_ctx_ = nullptr;
    }
}

bool FFmpegOutputBackend::initialize(const Configuration& config)
{
    // Store configuration
    active_area_only_ = config.active_area_only;
    
    // Parse format string (e.g., "mp4-h264", "mkv-ffv1")
    auto it = config.options.find("format");
    if (it == config.options.end()) {
        ORC_LOG_ERROR("FFmpegOutputBackend: No format specified in options");
        return false;
    }
    
    std::string format_str = it->second;
    size_t dash_pos = format_str.find('-');
    if (dash_pos == std::string::npos) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Invalid format string '{}' (expected 'container-codec')", format_str);
        return false;
    }
    
    container_format_ = format_str.substr(0, dash_pos);
    codec_name_ = format_str.substr(dash_pos + 1);
    
    // Store encoder quality settings
    encoder_preset_ = config.encoder_preset;
    encoder_crf_ = config.encoder_crf;
    encoder_bitrate_ = config.encoder_bitrate;
    
    // Map user-friendly container names to FFmpeg format names
    std::string ffmpeg_format = container_format_;
    if (container_format_ == "mkv") {
        ffmpeg_format = "matroska";
    }
    
    ORC_LOG_INFO("FFmpegOutputBackend: Initializing {} output with {} codec", container_format_, codec_name_);
    
    // Map codec names to FFmpeg codec IDs with fallbacks
    std::vector<std::string> codec_candidates;
    if (codec_name_ == "h264") {
        // Try encoders in order of preference: libx264, libopenh264, hardware encoders
        codec_candidates = {"libx264", "libopenh264", "h264_vaapi", "h264_qsv", "h264_nvenc"};
    } else if (codec_name_ == "h265" || codec_name_ == "hevc") {
        codec_candidates = {"libx265", "hevc_vaapi", "hevc_qsv", "hevc_nvenc"};
    } else if (codec_name_ == "ffv1") {
        codec_candidates = {"ffv1"};
    } else {
        ORC_LOG_ERROR("FFmpegOutputBackend: Unknown codec '{}'", codec_name_);
        return false;
    }
    
    // Allocate format context
    int ret = avformat_alloc_output_context2(&format_ctx_, nullptr, ffmpeg_format.c_str(), config.output_path.c_str());
    if (ret < 0 || !format_ctx_) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate output context: {}", errbuf);
        return false;
    }
    
    // Try encoders in order of preference
    bool encoder_found = false;
    std::string used_codec;
    for (const auto& candidate : codec_candidates) {
        ORC_LOG_DEBUG("FFmpegOutputBackend: Trying codec '{}'", candidate);
        if (setupEncoder(candidate, config.video_params)) {
            encoder_found = true;
            used_codec = candidate;
            ORC_LOG_INFO("FFmpegOutputBackend: Using codec '{}'", candidate);
            break;
        }
    }
    
    if (!encoder_found) {
        ORC_LOG_ERROR("FFmpegOutputBackend: No suitable {} encoder found", codec_name_);
        ORC_LOG_ERROR("FFmpegOutputBackend: Tried: {}", [&](){
            std::string list;
            for (size_t i = 0; i < codec_candidates.size(); i++) {
                if (i > 0) list += ", ";
                list += codec_candidates[i];
            }
            return list;
        }());
        cleanup();
        return false;
    }
    
    // Open output file
    ret = avio_open(&format_ctx_->pb, config.output_path.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to open output file '{}': {}", config.output_path, errbuf);
        cleanup();
        return false;
    }
    
    // Write file header
    ret = avformat_write_header(format_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to write header: {}", errbuf);
        cleanup();
        return false;
    }
    
    ORC_LOG_INFO("FFmpegOutputBackend: Initialized {} encoder ({}x{})", codec_name_, width_, height_);
    
    return true;
}

bool FFmpegOutputBackend::setupEncoder(const std::string& codec_id, const orc::VideoParameters& params)
{
    // Find encoder
    const AVCodec* codec = avcodec_find_encoder_by_name(codec_id.c_str());
    if (!codec) {
        ORC_LOG_DEBUG("FFmpegOutputBackend: Encoder '{}' not available", codec_id);
        return false;
    }
    
    // Create stream
    stream_ = avformat_new_stream(format_ctx_, nullptr);
    if (!stream_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create stream");
        return false;
    }
    stream_->id = format_ctx_->nb_streams - 1;
    
    // Allocate codec context
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate codec context");
        return false;
    }
    
    // Calculate dimensions with padding
    active_width_ = params.active_video_end - params.active_video_start;
    active_height_ = params.last_active_frame_line - params.first_active_frame_line;
    
    // Store video system and IRE levels for color space configuration
    video_system_ = params.system;
    black_ire_ = params.black_16b_ire;
    white_ire_ = params.white_16b_ire;
    
    // Apply padding for codec requirements (typically 16 for H.264/H.265)
    width_ = active_width_;
    height_ = active_height_;
    
    // Pad to multiple of 16 (required by most codecs) unless active area only
    if (!active_area_only_) {
        while (width_ % 16 != 0) width_++;
        while (height_ % 16 != 0) height_++;
    }
    
    // Set codec parameters
    codec_ctx_->codec_id = codec->id;
    codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx_->width = width_;
    codec_ctx_->height = height_;
    
    // Set frame rate
    if (params.system == VideoSystem::PAL || params.system == VideoSystem::PAL_M) {
        time_base_ = {1, 25};
    } else if (params.system == VideoSystem::NTSC) {
        time_base_ = {1001, 30000};  // 29.97 fps
    } else {
        time_base_ = {1, 25};  // Default to PAL
    }
    
    codec_ctx_->time_base = time_base_;
    codec_ctx_->framerate = av_inv_q(time_base_);
    stream_->time_base = time_base_;
    
    // Select pixel format based on what the encoder supports
    // Our source is YUV444P16LE, but most encoders don't support that
    // We'll convert during encoding using swscale
    if (codec_id == "libx264" || codec_id == "libx265") {
        // libx264/libx265 support high quality formats
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV444P;  // 8-bit 4:4:4
    } else if (codec_id == "libopenh264") {
        // libopenh264 only supports yuv420p
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    } else if (codec_id.find("_vaapi") != std::string::npos) {
        // VAAPI uses hardware surfaces, but we'll upload from yuv420p
        codec_ctx_->pix_fmt = AV_PIX_FMT_VAAPI;
    } else if (codec_id.find("_qsv") != std::string::npos) {
        // QSV uses hardware surfaces
        codec_ctx_->pix_fmt = AV_PIX_FMT_QSV;
    } else if (codec_id.find("_nvenc") != std::string::npos) {
        // NVENC typically uses NV12
        codec_ctx_->pix_fmt = AV_PIX_FMT_NV12;
    } else {
        // Default fallback
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    }
    
    // Codec-specific settings
    if (codec_id == "libx264" || codec_id == "libx265") {
        // Use user-specified preset and quality settings
        av_opt_set(codec_ctx_->priv_data, "preset", encoder_preset_.c_str(), 0);
        
        // Use CRF or bitrate based on encoder_bitrate setting
        if (encoder_bitrate_ > 0) {
            // Explicit bitrate mode
            codec_ctx_->bit_rate = encoder_bitrate_;
            ORC_LOG_DEBUG("FFmpegOutputBackend: Using bitrate mode: {} bps", encoder_bitrate_);
        } else {
            // CRF mode (constant quality)
            char crf_str[16];
            snprintf(crf_str, sizeof(crf_str), "%d", encoder_crf_);
            av_opt_set(codec_ctx_->priv_data, "crf", crf_str, 0);
            ORC_LOG_DEBUG("FFmpegOutputBackend: Using CRF mode: {}", encoder_crf_);
        }
    } else if (codec_id == "libopenh264") {
        // OpenH264 doesn't support CRF, use bitrate
        if (encoder_bitrate_ > 0) {
            codec_ctx_->bit_rate = encoder_bitrate_;
        } else {
            // Default to high bitrate if CRF specified
            codec_ctx_->bit_rate = 20000000;  // 20 Mbps for high quality
        }
    } else if (codec_id.find("_vaapi") != std::string::npos || 
               codec_id.find("_qsv") != std::string::npos ||
               codec_id.find("_nvenc") != std::string::npos) {
        // Hardware encoders - use bitrate mode
        if (encoder_bitrate_ > 0) {
            codec_ctx_->bit_rate = encoder_bitrate_;
        } else {
            codec_ctx_->bit_rate = 20000000;  // 20 Mbps default
        }
        if (codec_id.find("_vaapi") != std::string::npos) {
            av_opt_set(codec_ctx_->priv_data, "quality", "1", 0);  // Best quality for VAAPI
        } else if (codec_id.find("_nvenc") != std::string::npos) {
            av_opt_set(codec_ctx_->priv_data, "preset", "hq", 0);
            av_opt_set(codec_ctx_->priv_data, "rc", "vbr", 0);
        }
    }
    
    // Color properties (BT.601 for PAL/NTSC) - applies to all H.264/H.265 variants
    if (codec_id.find("264") != std::string::npos || 
        codec_id.find("265") != std::string::npos ||
        codec_id.find("hevc") != std::string::npos) {
        if (params.system == VideoSystem::PAL || params.system == VideoSystem::PAL_M) {
            codec_ctx_->color_primaries = AVCOL_PRI_BT470BG;
            codec_ctx_->color_trc = AVCOL_TRC_GAMMA28;
            codec_ctx_->colorspace = AVCOL_SPC_BT470BG;
        } else {
            codec_ctx_->color_primaries = AVCOL_PRI_SMPTE170M;
            codec_ctx_->color_trc = AVCOL_TRC_SMPTE170M;
            codec_ctx_->colorspace = AVCOL_SPC_SMPTE170M;
        }
        codec_ctx_->color_range = AVCOL_RANGE_MPEG;  // Limited range (TV)
    }
    
    // Enable multi-threaded encoding for better performance
    // Use all available CPU cores, but cap at 16 for efficiency
    unsigned int thread_count = std::min(std::thread::hardware_concurrency(), 16u);
    if (thread_count == 0) {
        thread_count = 4;  // Fallback if hardware_concurrency returns 0
    }
    
    codec_ctx_->thread_count = thread_count;
    codec_ctx_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;  // Enable both frame and slice threading
    
    ORC_LOG_INFO("FFmpegOutputBackend: Enabling multi-threaded encoding with {} threads", thread_count);
    
    // Some formats require global headers
    if (format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    // Open codec
    int ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to open codec: {}", errbuf);
        return false;
    }
    
    // Copy codec parameters to stream
    ret = avcodec_parameters_from_context(stream_->codecpar, codec_ctx_);
    if (ret < 0) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to copy codec parameters");
        return false;
    }
    
    // Allocate frame (destination - encoder's pixel format)
    frame_ = av_frame_alloc();
    if (!frame_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate frame");
        return false;
    }
    
    frame_->format = codec_ctx_->pix_fmt;
    frame_->width = codec_ctx_->width;
    frame_->height = codec_ctx_->height;
    
    ret = av_frame_get_buffer(frame_, 0);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate frame buffers: {}", errbuf);
        return false;
    }
    
    // Allocate source frame (YUV444P16LE from ComponentFrame)
    src_frame_ = av_frame_alloc();
    if (!src_frame_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate source frame");
        return false;
    }
    
    src_frame_->format = AV_PIX_FMT_YUV444P16LE;
    src_frame_->width = width_;
    src_frame_->height = height_;
    
    ret = av_frame_get_buffer(src_frame_, 0);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate source frame buffers: {}", errbuf);
        return false;
    }
    
    // Initialize swscale context for pixel format conversion with proper color space handling
    sws_ctx_ = sws_getContext(
        width_, height_, AV_PIX_FMT_YUV444P16LE,  // Source
        width_, height_, codec_ctx_->pix_fmt,      // Destination
        SWS_LANCZOS, nullptr, nullptr, nullptr     // High quality scaling
    );
    if (!sws_ctx_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create swscale context");
        return false;
    }
    
    // Configure color space conversion
    // ComponentFrame uses IRE scale with black_16b_ire/white_16b_ire range
    // OutputWriter converts this to limited range Y'CbCr (16-235/240 for 8-bit, scaled to 16-bit)
    // So our source is already in "video" range, not full range
    int colorspace = SWS_CS_ITU601;
    
    // Set colorspace based on video system
    if (video_system_ == VideoSystem::PAL || video_system_ == VideoSystem::PAL_M) {
        colorspace = SWS_CS_ITU601;
    } else {
        colorspace = SWS_CS_SMPTE170M;  // NTSC
    }
    
    // Both source and destination are limited (broadcast) range
    const int src_range = 0;  // Limited range (video levels)
    const int dst_range = 0;  // Limited range (broadcast)
    
    const int* coefficients = sws_getCoefficients(colorspace);
    
    sws_setColorspaceDetails(sws_ctx_, 
        coefficients, src_range,  // Source
        coefficients, dst_range,  // Destination
        0, 1 << 16, 1 << 16);
    
    ORC_LOG_DEBUG("FFmpegOutputBackend: Configured color conversion: limited→limited range, colorspace {}", 
                  video_system_ == VideoSystem::PAL ? "BT.601 (PAL)" : "SMPTE170M (NTSC)");
    
    // Allocate packet
    packet_ = av_packet_alloc();
    if (!packet_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate packet");
        return false;
    }
    
    return true;
}

bool FFmpegOutputBackend::writeFrame(const ComponentFrame& component_frame)
{
    if (!codec_ctx_ || !frame_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Not initialized");
        return false;
    }
    
    return convertAndEncode(component_frame);
}

bool FFmpegOutputBackend::convertAndEncode(const ComponentFrame& component_frame)
{
    // Make source frame writable
    int ret = av_frame_make_writable(src_frame_);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to make source frame writable: {}", errbuf);
        return false;
    }
    
    // Copy Y, U, V planes from ComponentFrame to source AVFrame (YUV444P16LE)
    // ComponentFrame uses IRE scale, need to convert to Y'CbCr limited range (BT.601)
    // This matches what OutputWriter::convertLine does for YUV444P16 output
    
    // Constants from outputwriter.cpp
    constexpr double Y_MIN   = 1.0    * 256.0;
    constexpr double Y_ZERO  = 16.0   * 256.0;  // 4096
    constexpr double Y_SCALE = 219.0  * 256.0;  // 56064
    constexpr double Y_MAX   = 254.75 * 256.0;
    
    constexpr double C_ZERO  = 128.0  * 256.0;  // 32768
    constexpr double C_SCALE = 224.0  * 256.0;  // 57344
    constexpr double C_MIN   = 1.0    * 256.0;
    constexpr double C_MAX   = 254.75 * 256.0;
    
    // BT.601 coefficients (from outputwriter.cpp)
    constexpr double kB = 0.87728321993817866838972487283129;
    constexpr double kR = 0.87728321993817866838972487283129;
    constexpr double ONE_MINUS_Kb = 1.0 - 0.114;
    constexpr double ONE_MINUS_Kr = 1.0 - 0.299;
    
    const double yOffset = black_ire_;
    const double yRange = white_ire_ - black_ire_;
    const double uvRange = yRange;
    
    const double yScale = Y_SCALE / yRange;
    const double cbScale = (C_SCALE / (ONE_MINUS_Kb * kB)) / uvRange;
    const double crScale = (C_SCALE / (ONE_MINUS_Kr * kR)) / uvRange;
    
    for (int y = 0; y < active_height_; y++) {
        const double* src_y = component_frame.y(y);
        const double* src_u = component_frame.u(y);
        const double* src_v = component_frame.v(y);
        
        uint16_t* dst_y = reinterpret_cast<uint16_t*>(src_frame_->data[0] + y * src_frame_->linesize[0]);
        uint16_t* dst_u = reinterpret_cast<uint16_t*>(src_frame_->data[1] + y * src_frame_->linesize[1]);
        uint16_t* dst_v = reinterpret_cast<uint16_t*>(src_frame_->data[2] + y * src_frame_->linesize[2]);
        
        for (int x = 0; x < active_width_; x++) {
            // Convert Y'UV (IRE scale) to Y'CbCr (limited range) - same as OutputWriter
            dst_y[x] = static_cast<uint16_t>(std::clamp(((src_y[x] - yOffset) * yScale)  + Y_ZERO, Y_MIN, Y_MAX));
            dst_u[x] = static_cast<uint16_t>(std::clamp((src_u[x]             * cbScale) + C_ZERO, C_MIN, C_MAX));
            dst_v[x] = static_cast<uint16_t>(std::clamp((src_v[x]             * crScale) + C_ZERO, C_MIN, C_MAX));
        }
        
        // Fill padding with black/neutral values if needed
        for (int x = active_width_; x < width_; x++) {
            dst_y[x] = static_cast<uint16_t>(Y_ZERO);    // Black (16*256)
            dst_u[x] = static_cast<uint16_t>(C_ZERO);    // Neutral chroma (128*256)
            dst_v[x] = static_cast<uint16_t>(C_ZERO);
        }
    }
    
    // Fill padding lines if needed
    for (int y = active_height_; y < height_; y++) {
        uint16_t* dst_y = reinterpret_cast<uint16_t*>(src_frame_->data[0] + y * src_frame_->linesize[0]);
        uint16_t* dst_u = reinterpret_cast<uint16_t*>(src_frame_->data[1] + y * src_frame_->linesize[1]);
        uint16_t* dst_v = reinterpret_cast<uint16_t*>(src_frame_->data[2] + y * src_frame_->linesize[2]);
        
        for (int x = 0; x < width_; x++) {
            dst_y[x] = static_cast<uint16_t>(Y_ZERO);
            dst_u[x] = static_cast<uint16_t>(C_ZERO);
            dst_v[x] = static_cast<uint16_t>(C_ZERO);
        }
    }
    
    // Convert from YUV444P16LE to encoder's pixel format using swscale
    ret = sws_scale(
        sws_ctx_,
        src_frame_->data, src_frame_->linesize, 0, height_,
        frame_->data, frame_->linesize
    );
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to convert pixel format: {}", errbuf);
        return false;
    }
    
    // Set presentation timestamp
    frame_->pts = pts_++;
    
    // Send frame to encoder
    ret = avcodec_send_frame(codec_ctx_, frame_);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to send frame to encoder: {}", errbuf);
        return false;
    }
    
    // Receive encoded packets
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            ORC_LOG_ERROR("FFmpegOutputBackend: Error receiving packet: {}", errbuf);
            return false;
        }
        
        // Rescale packet timestamps
        av_packet_rescale_ts(packet_, codec_ctx_->time_base, stream_->time_base);
        packet_->stream_index = stream_->index;
        
        // Write packet to file
        ret = av_interleaved_write_frame(format_ctx_, packet_);
        av_packet_unref(packet_);
        
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            ORC_LOG_ERROR("FFmpegOutputBackend: Error writing packet: {}", errbuf);
            return false;
        }
    }
    
    frames_written_++;
    return true;
}

bool FFmpegOutputBackend::finalize()
{
    if (!codec_ctx_ || !format_ctx_) {
        return true;  // Already finalized
    }
    
    // Flush encoder
    int ret = avcodec_send_frame(codec_ctx_, nullptr);
    if (ret < 0) {
        ORC_LOG_WARN("FFmpegOutputBackend: Error flushing encoder");
    }
    
    // Receive remaining packets
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }
        
        av_packet_rescale_ts(packet_, codec_ctx_->time_base, stream_->time_base);
        packet_->stream_index = stream_->index;
        av_interleaved_write_frame(format_ctx_, packet_);
        av_packet_unref(packet_);
    }
    
    // Write trailer
    av_write_trailer(format_ctx_);
    
    ORC_LOG_INFO("FFmpegOutputBackend: Encoded {} frames", frames_written_);
    
    cleanup();
    return true;
}

std::string FFmpegOutputBackend::getFormatInfo() const
{
    return container_format_ + " (" + codec_name_ + ")";
}

} // namespace orc

#endif // HAVE_FFMPEG
