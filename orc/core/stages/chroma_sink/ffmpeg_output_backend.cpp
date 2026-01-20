/*
 * File:        ffmpeg_output_backend.cpp
 * Module:      orc-core
 * Purpose:     FFmpeg-based video encoding implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "ffmpeg_output_backend.h"

#ifdef HAVE_FFMPEG

#include "componentframe.h"
#include "logging.h"
#include "video_field_representation.h"
#include "tbc_video_field_representation.h"
#include "field_id.h"
#include "closed_caption_observer.h"
#include "observation_history.h"
#include "eia608_decoder.h"
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
    if (audio_packet_) {
        av_packet_free(&audio_packet_);
        audio_packet_ = nullptr;
    }
    
    if (audio_frame_) {
        av_frame_free(&audio_frame_);
        audio_frame_ = nullptr;
    }
    
    if (audio_codec_ctx_) {
        avcodec_free_context(&audio_codec_ctx_);
        audio_codec_ctx_ = nullptr;
    }
    
    // subtitle_codec_ctx_ is just a marker (non-null = enabled), not a real context
    subtitle_codec_ctx_ = nullptr;
    
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
    
    // Get hardware encoder preference
    std::string hardware_encoder = "none";
    auto hw_it = config.options.find("hardware_encoder");
    if (hw_it != config.options.end()) {
        hardware_encoder = hw_it->second;
    }
    
    // Get ProRes profile preference
    std::string prores_profile = "hq";
    auto prores_it = config.options.find("prores_profile");
    if (prores_it != config.options.end()) {
        prores_profile = prores_it->second;
    }
    
    // Get lossless mode preference
    bool use_lossless = false;
    auto lossless_it = config.options.find("use_lossless_mode");
    if (lossless_it != config.options.end()) {
        use_lossless = (lossless_it->second == "true" || lossless_it->second == "1");
    }
    
    // Store encoder quality settings
    encoder_preset_ = config.encoder_preset;
    encoder_crf_ = config.encoder_crf;
    encoder_bitrate_ = config.encoder_bitrate;
    
    // Store audio configuration
    embed_audio_ = config.embed_audio;
    embed_closed_captions_ = config.embed_closed_captions;
    vfr_ = config.vfr;
    start_field_index_ = config.start_field_index;
    num_fields_ = config.num_fields;
    current_field_for_audio_ = start_field_index_;
    current_field_for_captions_ = start_field_index_;
    
    // Map user-friendly container names to FFmpeg format names
    std::string ffmpeg_format = container_format_;
    if (container_format_ == "mkv") {
        ffmpeg_format = "matroska";
    } else if (container_format_ == "mov") {
        ffmpeg_format = "mov";
    } else if (container_format_ == "mxf") {
        ffmpeg_format = "mxf_d10";  // D10 variant
    } else if (container_format_ == "mp4") {
        ffmpeg_format = "mp4";
    }
    
    ORC_LOG_DEBUG("FFmpegOutputBackend: Initializing {} output with {} codec (hardware: {}, lossless: {})", 
                  container_format_, codec_name_, hardware_encoder, use_lossless);
    
    // Map codec names to FFmpeg codec IDs with fallbacks
    std::vector<std::string> codec_candidates;
    if (codec_name_ == "h264") {
        // Apply hardware encoder preference
        if (hardware_encoder == "vaapi") {
            codec_candidates = {"h264_vaapi", "libx264"};
        } else if (hardware_encoder == "nvenc") {
            codec_candidates = {"h264_nvenc", "libx264"};
        } else if (hardware_encoder == "qsv") {
            codec_candidates = {"h264_qsv", "libx264"};
        } else if (hardware_encoder == "amf") {
            codec_candidates = {"h264_amf", "libx264"};
        } else if (hardware_encoder == "videotoolbox") {
            codec_candidates = {"h264_videotoolbox", "libx264"};
        } else {
            // None/auto: prefer software encoder
            codec_candidates = {"libx264", "libopenh264"};
        }
    } else if (codec_name_ == "hevc") {
        // Apply hardware encoder preference
        if (hardware_encoder == "vaapi") {
            codec_candidates = {"hevc_vaapi", "libx265"};
        } else if (hardware_encoder == "nvenc") {
            codec_candidates = {"hevc_nvenc", "libx265"};
        } else if (hardware_encoder == "qsv") {
            codec_candidates = {"hevc_qsv", "libx265"};
        } else if (hardware_encoder == "amf") {
            codec_candidates = {"hevc_amf", "libx265"};
        } else if (hardware_encoder == "videotoolbox") {
            codec_candidates = {"hevc_videotoolbox", "libx265"};
        } else {
            codec_candidates = {"libx265"};
        }
    } else if (codec_name_ == "av1") {
        // Apply hardware encoder preference
        if (hardware_encoder == "vaapi") {
            codec_candidates = {"av1_vaapi", "libsvtav1", "libaom-av1"};
        } else if (hardware_encoder == "nvenc") {
            codec_candidates = {"av1_nvenc", "libsvtav1", "libaom-av1"};
        } else if (hardware_encoder == "qsv") {
            codec_candidates = {"av1_qsv", "libsvtav1", "libaom-av1"};
        } else if (hardware_encoder == "amf") {
            codec_candidates = {"av1_amf", "libsvtav1", "libaom-av1"};
        } else {
            codec_candidates = {"libsvtav1", "libaom-av1"};
        }
    } else if (codec_name_ == "prores") {
        // Apply ProRes profile preference
        if (hardware_encoder == "videotoolbox") {
            codec_candidates = {"prores_videotoolbox", "prores_ks", "prores"};
        } else {
            // Software encoder
            codec_candidates = {"prores_ks", "prores"};
        }
    } else if (codec_name_ == "ffv1") {
        codec_candidates = {"ffv1"};
    } else if (codec_name_ == "v210") {
        codec_candidates = {"v210"};
    } else if (codec_name_ == "v410") {
        codec_candidates = {"v410"};
    } else if (codec_name_ == "mpeg2video") {
        codec_candidates = {"mpeg2video"};
    } else {
        ORC_LOG_ERROR("FFmpegOutputBackend: Unknown codec '{}'", codec_name_);
        return false;
    }
    
    // Store lossless mode for later use in setupEncoder
    use_lossless_mode_ = use_lossless;
    prores_profile_ = prores_profile;
    
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
            ORC_LOG_DEBUG("FFmpegOutputBackend: Using codec '{}'", candidate);
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
    
    // Setup audio encoder if requested
    if (embed_audio_ && vfr_ && vfr_->has_audio()) {
        ORC_LOG_DEBUG("FFmpegOutputBackend: Setting up audio encoder");
        if (!setupAudioEncoder()) {
            ORC_LOG_ERROR("FFmpegOutputBackend: Failed to setup audio encoder");
            cleanup();
            return false;
        }
    } else if (embed_audio_) {
        ORC_LOG_WARN("FFmpegOutputBackend: Audio embedding requested but no audio available");
        embed_audio_ = false;  // Disable audio
    }
    
    // Setup subtitle encoder for closed captions if requested
    // Note: Closed captions are only supported in MP4/MOV containers
    if (embed_closed_captions_ && vfr_) {
        if (container_format_ != "mp4" && container_format_ != "mov") {
            ORC_LOG_WARN("FFmpegOutputBackend: Closed captions only supported in MP4/MOV containers, disabling");
            embed_closed_captions_ = false;
        } else {
            ORC_LOG_DEBUG("FFmpegOutputBackend: Setting up subtitle encoder for closed captions");
            if (!setupSubtitleEncoder()) {
                ORC_LOG_ERROR("FFmpegOutputBackend: Failed to setup subtitle encoder");
                cleanup();
                return false;
            }
            
            // Process all fields to extract closed caption cues
            ORC_LOG_DEBUG("FFmpegOutputBackend: Decoding closed captions from all fields");
            eia608_decoder_ = std::make_unique<EIA608Decoder>();
        }
    }
    
    if (embed_closed_captions_) {
        ORC_LOG_WARN("FFmpegOutputBackend: Closed caption embedding disabled (legacy observers removed)");
        embed_closed_captions_ = false;
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
    
    ORC_LOG_DEBUG("FFmpegOutputBackend: Initialized {} encoder ({}x{})", codec_name_, width_, height_);
    
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
    
    // Allocate codec context first (before creating stream)
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate codec context");
        return false;
    }
    
    // Calculate dimensions from video parameters (active area only)
    active_width_ = params.active_video_end - params.active_video_start;
    active_height_ = params.last_active_frame_line - params.first_active_frame_line;
    
    // Store video system, IRE levels, and full parameters for color space configuration
    video_system_ = params.system;
    black_ire_ = params.black_16b_ire;
    white_ire_ = params.white_16b_ire;
    video_params_ = params;  // Store full params for offset handling in convertAndEncode
    
    // Set source and output dimensions to active area
    src_width_ = active_width_;
    src_height_ = active_height_;
    
    // Ensure dimensions are even (required by most video codecs, especially H.264/H.265)
    // If dimensions are odd, round up to the next even number
    width_ = (src_width_ + 1) & ~1;   // Round up to even
    height_ = (src_height_ + 1) & ~1; // Round up to even
    
    if (width_ != src_width_ || height_ != src_height_) {
        ORC_LOG_DEBUG("FFmpegOutputBackend: Padding dimensions from {}x{} to {}x{} (codecs require even dimensions)", 
                     src_width_, src_height_, width_, height_);
    }
    
    crop_top_ = 0;
    
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
    
    // Select pixel format based on what the encoder supports
    // Our source is YUV444P16LE, but most encoders don't support that
    // We'll convert during encoding using swscale
    if (codec_id == "ffv1") {
        // FFV1 supports high bit depth
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV422P10LE;
    } else if (codec_id == "prores" || codec_id == "prores_ks") {
        // ProRes format depends on profile
        if (codec_name_ == "prores_4444" || codec_name_ == "prores_4444xq") {
            codec_ctx_->pix_fmt = AV_PIX_FMT_YUV444P10LE;
        } else {
            codec_ctx_->pix_fmt = AV_PIX_FMT_YUV422P10LE;
        }
    } else if (codec_id == "prores_videotoolbox") {
        // VideoToolbox ProRes uses different formats
        if (codec_name_ == "prores_4444" || codec_name_ == "prores_4444xq") {
            codec_ctx_->pix_fmt = AV_PIX_FMT_P416LE;
        } else {
            codec_ctx_->pix_fmt = AV_PIX_FMT_UYVY422;
        }
    } else if (codec_id == "v210") {
        // V210 is 10-bit 4:2:2
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV422P10LE;
    } else if (codec_id == "v410") {
        // V410 is 10-bit 4:4:4
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV444P10LE;
    } else if (codec_id == "mpeg2video") {
        // MPEG-2 (D10) uses 8-bit 4:2:2
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV422P;
    } else if (codec_id == "libx264" || codec_id == "libx265") {
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
    } else if (codec_id.find("_amf") != std::string::npos) {
        // AMD AMF uses NV12
        codec_ctx_->pix_fmt = AV_PIX_FMT_NV12;
    } else if (codec_id.find("_videotoolbox") != std::string::npos) {
        // Apple VideoToolbox
        codec_ctx_->pix_fmt = AV_PIX_FMT_VIDEOTOOLBOX;
    } else if (codec_id == "libsvtav1" || codec_id == "libaom-av1") {
        // AV1 encoders
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    } else {
        // Default fallback
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    }
    
    // Codec-specific settings
    if (codec_id == "ffv1") {
        // FFV1 lossless settings (from tbc-video-export)
        av_opt_set(codec_ctx_->priv_data, "coder", "1", 0);
        av_opt_set(codec_ctx_->priv_data, "context", "1", 0);
        av_opt_set(codec_ctx_->priv_data, "slices", "4", 0);
        av_opt_set(codec_ctx_->priv_data, "slicecrc", "1", 0);
        av_opt_set_int(codec_ctx_->priv_data, "level", 3, 0);
        codec_ctx_->gop_size = 1;  // Intra-only
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using FFV1 lossless settings");
    } else if (codec_id == "prores" || codec_id == "prores_ks") {
        // ProRes settings - use prores_profile_ parameter
        int profile_num = 3;  // Default to HQ
        if (prores_profile_ == "proxy") {
            profile_num = 0;
        } else if (prores_profile_ == "lt") {
            profile_num = 1;
        } else if (prores_profile_ == "standard") {
            profile_num = 2;
        } else if (prores_profile_ == "hq") {
            profile_num = 3;
        } else if (prores_profile_ == "4444") {
            profile_num = 4;
            codec_ctx_->pix_fmt = AV_PIX_FMT_YUV444P10LE;  // 4444 needs 4:4:4
        } else if (prores_profile_ == "4444xq" || prores_profile_ == "xq") {
            profile_num = 5;
            codec_ctx_->pix_fmt = AV_PIX_FMT_YUV444P10LE;  // XQ needs 4:4:4
        }
        
        char profile_str[16];
        snprintf(profile_str, sizeof(profile_str), "%d", profile_num);
        av_opt_set(codec_ctx_->priv_data, "profile", profile_str, 0);
        av_opt_set(codec_ctx_->priv_data, "vendor", "apl0", 0);
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using ProRes profile: {}", prores_profile_);
    } else if (codec_id == "prores_videotoolbox") {
        // Apple VideoToolbox ProRes - use prores_profile_ parameter
        std::string vt_profile = "hq";  // Default
        if (prores_profile_ == "4444xq" || prores_profile_ == "xq") {
            vt_profile = "xq";
            codec_ctx_->pix_fmt = AV_PIX_FMT_P416LE;  // XQ needs 4:4:4
        } else if (prores_profile_ == "4444") {
            vt_profile = "4444";
            codec_ctx_->pix_fmt = AV_PIX_FMT_P416LE;  // 4444 needs 4:4:4
        } else {
            vt_profile = prores_profile_;  // Use as-is for proxy/lt/standard/hq
        }
        
        av_opt_set(codec_ctx_->priv_data, "profile", vt_profile.c_str(), 0);
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using ProRes VideoToolbox profile: {}", vt_profile);
    } else if (codec_id == "v210" || codec_id == "v410") {
        // V210/V410 are uncompressed, no special settings needed
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using uncompressed {} codec", codec_id);
    } else if (codec_id == "mpeg2video") {
        // D10 (Sony IMX/XDCAM) settings
        bool is_pal = (params.system == VideoSystem::PAL || params.system == VideoSystem::PAL_M);
        int64_t bitrate = is_pal ? 50000000 : 49999840;  // 50 Mbps PAL, ~50 Mbps NTSC
        int bufsize = is_pal ? 2000000 : 1668328;
        
        codec_ctx_->bit_rate = bitrate;
        codec_ctx_->rc_min_rate = bitrate;
        codec_ctx_->rc_max_rate = bitrate;
        codec_ctx_->rc_buffer_size = bufsize;
        codec_ctx_->rc_initial_buffer_occupancy = bufsize;
        codec_ctx_->gop_size = 1;  // Intra-only (I-frame only)
        codec_ctx_->qmin = 1;
        codec_ctx_->qmax = 3;
        
        av_opt_set(codec_ctx_->priv_data, "intra_vlc", "1", 0);
        av_opt_set(codec_ctx_->priv_data, "non_linear_quant", "1", 0);
        av_opt_set_int(codec_ctx_->priv_data, "dc", 10, 0);
        av_opt_set_int(codec_ctx_->priv_data, "ps", 1, 0);
        
        // Set interlaced flags
        codec_ctx_->flags |= AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME | AV_CODEC_FLAG_LOW_DELAY;
        
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using D10 settings ({})", is_pal ? "PAL" : "NTSC");
    } else if (codec_id == "libx264" || codec_id == "libx265") {
        // Use user-specified preset and quality settings
        av_opt_set(codec_ctx_->priv_data, "preset", encoder_preset_.c_str(), 0);
        
        if (use_lossless_mode_) {
            // Lossless mode
            av_opt_set(codec_ctx_->priv_data, "qp", "0", 0);
            if (codec_id == "libx264") {
                codec_ctx_->pix_fmt = AV_PIX_FMT_YUV444P;  // Use 4:4:4 for lossless
            }
            ORC_LOG_DEBUG("FFmpegOutputBackend: Using lossless mode");
        } else if (encoder_bitrate_ > 0) {
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
        
        // Add interlaced flag if not deinterlacing
        // TODO: check for apply_deinterlace parameter
        if (codec_id == "libx264") {
            av_opt_set(codec_ctx_->priv_data, "x264opts", "interlaced=1", 0);
        } else {
            av_opt_set(codec_ctx_->priv_data, "x265-params", "interlace=true", 0);
        }
    } else if (codec_id == "libsvtav1" || codec_id == "libaom-av1") {
        // AV1 encoder settings
        if (use_lossless_mode_) {
            // Lossless AV1
            if (codec_id == "libaom-av1") {
                av_opt_set(codec_ctx_->priv_data, "cpu-used", "4", 0);
                av_opt_set(codec_ctx_->priv_data, "crf", "0", 0);
                av_opt_set(codec_ctx_->priv_data, "lossless", "1", 0);
            } else {
                // SVT-AV1 doesn't have direct lossless mode, use CRF=0
                av_opt_set(codec_ctx_->priv_data, "crf", "0", 0);
            }
            ORC_LOG_DEBUG("FFmpegOutputBackend: Using AV1 lossless mode");
        } else {
            // Quality mode
            if (codec_id == "libsvtav1") {
                av_opt_set(codec_ctx_->priv_data, "preset", "6", 0);
                av_opt_set_int(codec_ctx_->priv_data, "crf", encoder_crf_, 0);
            } else {
                av_opt_set(codec_ctx_->priv_data, "cpu-used", "4", 0);
                av_opt_set_int(codec_ctx_->priv_data, "crf", encoder_crf_, 0);
            }
            ORC_LOG_DEBUG("FFmpegOutputBackend: Using AV1 CRF mode: {}", encoder_crf_);
        }
    } else if (codec_id == "h264_vaapi" || codec_id == "hevc_vaapi") {
        // VA-API settings
        av_opt_set(codec_ctx_->priv_data, "rc_mode", "CQP", 0);
        av_opt_set_int(codec_ctx_->priv_data, "global_quality", 24, 0);
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using VA-API settings");
    } else if (codec_id == "h264_nvenc" || codec_id == "hevc_nvenc") {
        // NVENC settings
        av_opt_set(codec_ctx_->priv_data, "rc", "constqp", 0);
        int qp = (codec_id == "h264_nvenc") ? 22 : 24;
        av_opt_set_int(codec_ctx_->priv_data, "qp", qp, 0);
        if (codec_id == "hevc_nvenc") {
            av_opt_set_int(codec_ctx_->priv_data, "b_ref_mode", 0, 0);
        }
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using NVENC settings");
    } else if (codec_id == "h264_qsv" || codec_id == "hevc_qsv") {
        // Intel QuickSync settings
        av_opt_set_int(codec_ctx_->priv_data, "global_quality", 19, 0);
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using QuickSync settings");
    } else if (codec_id == "h264_amf" || codec_id == "hevc_amf") {
        // AMD AMF settings
        av_opt_set_int(codec_ctx_->priv_data, "quality", 2, 0);
        av_opt_set(codec_ctx_->priv_data, "rc", "cqp", 0);
        av_opt_set_int(codec_ctx_->priv_data, "qp_i", 28, 0);
        av_opt_set_int(codec_ctx_->priv_data, "qp_p", 28, 0);
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using AMF settings");
    } else if (codec_id == "h264_videotoolbox" || codec_id == "hevc_videotoolbox") {
        // Apple VideoToolbox settings
        av_opt_set(codec_ctx_->priv_data, "profile", "main", 0);
        av_opt_set_int(codec_ctx_->priv_data, "q", 60, 0);
        if (codec_id == "hevc_videotoolbox") {
            stream_->codecpar->codec_tag = MKTAG('h','v','c','1');
        } else {
            stream_->codecpar->codec_tag = MKTAG('h','v','c','1');
        }
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using VideoToolbox settings");
    } else if (codec_id == "libsvtav1") {
        // SVT-AV1 settings
        av_opt_set_int(codec_ctx_->priv_data, "crf", 24, 0);
        av_opt_set_int(codec_ctx_->priv_data, "cpu-used", 6, 0);
        av_opt_set_int(codec_ctx_->priv_data, "row-mt", 1, 0);
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using SVT-AV1 settings");
    } else if (codec_id == "libaom-av1") {
        // libaom-av1 settings
        if (codec_name_ == "av1_lossless") {
            av_opt_set_int(codec_ctx_->priv_data, "crf", 0, 0);
            av_opt_set(codec_ctx_->priv_data, "aom-params", "lossless=1", 0);
        } else {
            av_opt_set_int(codec_ctx_->priv_data, "crf", 24, 0);
        }
        av_opt_set_int(codec_ctx_->priv_data, "cpu-used", 6, 0);
        av_opt_set_int(codec_ctx_->priv_data, "row-mt", 1, 0);
        av_opt_set_int(codec_ctx_->priv_data, "error-resilience", 1, 0);
        ORC_LOG_DEBUG("FFmpegOutputBackend: Using libaom-av1 settings");
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
        // Hardware encoders - use bitrate mode (fallback for unhandled variants)
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
    
    ORC_LOG_DEBUG("FFmpegOutputBackend: Enabling multi-threaded encoding with {} threads", thread_count);
    
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
        avcodec_free_context(&codec_ctx_);
        return false;
    }
    
    // Now create the stream (only after codec is successfully opened)
    stream_ = avformat_new_stream(format_ctx_, nullptr);
    if (!stream_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create stream");
        avcodec_free_context(&codec_ctx_);
        return false;
    }
    stream_->id = format_ctx_->nb_streams - 1;
    stream_->time_base = time_base_;
    
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
    
    // Encode audio for this frame first
    if (!encodeAudioForFrame()) {
        return false;
    }
    
    // Encode closed captions for this frame
    if (!encodeClosedCaptionsForFrame()) {
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
    constexpr double C_SCALE = 112.0  * 256.0;  // 28672
    constexpr double C_MIN   = 1.0    * 256.0;
    constexpr double C_MAX   = 254.75 * 256.0;
    
    // BT.601 coefficients (from outputwriter.cpp)
    // kB = sqrt(209556997.0 / 96146491.0) / 3.0
    // kR = sqrt(221990474.0 / 288439473.0)
    constexpr double kB = 0.49211104112248356308804691718185;
    constexpr double kR = 0.87728321993817866838972487283129;
    constexpr double ONE_MINUS_Kb = 1.0 - 0.114;
    constexpr double ONE_MINUS_Kr = 1.0 - 0.299;
    
    const double yOffset = black_ire_;
    const double yRange = white_ire_ - black_ire_;
    const double uvRange = yRange;
    
    const double yScale = Y_SCALE / yRange;
    const double cbScale = (C_SCALE / (ONE_MINUS_Kb * kB)) / uvRange;
    const double crScale = (C_SCALE / (ONE_MINUS_Kr * kR)) / uvRange;
    
    // Handle offset into ComponentFrame (matches OutputWriter logic)
    // When cropping is applied, componentFrame is indexed from 0
    // Otherwise, it's indexed from first_active_frame_line
    const int32_t inputLineOffset = video_params_.active_area_cropping_applied ? 0 : 
                                    video_params_.first_active_frame_line;
    const int32_t xOffset = video_params_.active_area_cropping_applied ? 0 : 
                           video_params_.active_video_start;
    
    // Copy active video lines from ComponentFrame
    for (int y = 0; y < src_height_; y++) {
        const int32_t inputLine = inputLineOffset + y;
        const double* src_y = component_frame.y(inputLine) + xOffset;
        const double* src_u = component_frame.u(inputLine) + xOffset;
        const double* src_v = component_frame.v(inputLine) + xOffset;
        
        uint16_t* dst_y = reinterpret_cast<uint16_t*>(src_frame_->data[0] + y * src_frame_->linesize[0]);
        uint16_t* dst_u = reinterpret_cast<uint16_t*>(src_frame_->data[1] + y * src_frame_->linesize[1]);
        uint16_t* dst_v = reinterpret_cast<uint16_t*>(src_frame_->data[2] + y * src_frame_->linesize[2]);
        
        for (int x = 0; x < src_width_; x++) {
            // Convert Y'UV (IRE scale) to Y'CbCr (limited range) - same as OutputWriter
            dst_y[x] = static_cast<uint16_t>(std::clamp(((src_y[x] - yOffset) * yScale)  + Y_ZERO, Y_MIN, Y_MAX));
            dst_u[x] = static_cast<uint16_t>(std::clamp((src_u[x]             * cbScale) + C_ZERO, C_MIN, C_MAX));
            dst_v[x] = static_cast<uint16_t>(std::clamp((src_v[x]             * crScale) + C_ZERO, C_MIN, C_MAX));
        }
        
        // Fill padding with black/neutral values if needed
        for (int x = src_width_; x < width_; x++) {
            dst_y[x] = static_cast<uint16_t>(Y_ZERO);    // Black (16*256)
            dst_u[x] = static_cast<uint16_t>(C_ZERO);    // Neutral chroma (128*256)
            dst_v[x] = static_cast<uint16_t>(C_ZERO);
        }
    }
    
    // Fill padding lines if needed
    for (int y = src_height_; y < height_; y++) {
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
    
    // Flush video encoder
    int ret = avcodec_send_frame(codec_ctx_, nullptr);
    if (ret < 0) {
        ORC_LOG_WARN("FFmpegOutputBackend: Error flushing video encoder");
    }
    
    // Receive remaining video packets
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
    
    // Flush audio encoder if present
    if (audio_codec_ctx_) {
        // Encode any remaining audio in the buffer (pad with silence if needed)
        int frame_size = audio_codec_ctx_->frame_size;
        if (!audio_buffer_.empty()) {
            ORC_LOG_DEBUG("FFmpegOutputBackend: Flushing {} remaining audio samples", audio_buffer_.size() / 2);
            
            // Pad buffer to frame_size if needed
            while (audio_buffer_.size() < static_cast<size_t>(frame_size * 2)) {
                audio_buffer_.push_back(0);  // Pad with silence
            }
            
            // Encode the final frame
            av_frame_make_writable(audio_frame_);
            float* left_channel = reinterpret_cast<float*>(audio_frame_->data[0]);
            float* right_channel = reinterpret_cast<float*>(audio_frame_->data[1]);
            
            for (int i = 0; i < frame_size; i++) {
                left_channel[i] = audio_buffer_[i * 2] / 32768.0f;
                right_channel[i] = audio_buffer_[i * 2 + 1] / 32768.0f;
            }
            
            audio_frame_->pts = audio_pts_;
            
            ret = avcodec_send_frame(audio_codec_ctx_, audio_frame_);
            if (ret >= 0) {
                while (ret >= 0) {
                    ret = avcodec_receive_packet(audio_codec_ctx_, audio_packet_);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret >= 0) {
                        av_packet_rescale_ts(audio_packet_, audio_codec_ctx_->time_base, audio_stream_->time_base);
                        audio_packet_->stream_index = audio_stream_->index;
                        av_interleaved_write_frame(format_ctx_, audio_packet_);
                        av_packet_unref(audio_packet_);
                    }
                }
            }
            
            audio_buffer_.clear();
        }
        
        // Flush the audio encoder
        ret = avcodec_send_frame(audio_codec_ctx_, nullptr);
        if (ret < 0) {
            ORC_LOG_WARN("FFmpegOutputBackend: Error flushing audio encoder");
        }
        
        while (ret >= 0) {
            ret = avcodec_receive_packet(audio_codec_ctx_, audio_packet_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                break;
            }
            
            av_packet_rescale_ts(audio_packet_, audio_codec_ctx_->time_base, audio_stream_->time_base);
            audio_packet_->stream_index = audio_stream_->index;
            av_interleaved_write_frame(format_ctx_, audio_packet_);
            av_packet_unref(audio_packet_);
        }
    }
    
    // Write trailer
    av_write_trailer(format_ctx_);
    
    ORC_LOG_DEBUG("FFmpegOutputBackend: Encoded {} frames", frames_written_);
    
    cleanup();
    return true;
}

std::string FFmpegOutputBackend::getFormatInfo() const
{
    std::string info = container_format_ + " (" + codec_name_;
    if (embed_audio_) info += " + audio";
    if (embed_closed_captions_) info += " + CC";
    info += ")";
    return info;
}

bool FFmpegOutputBackend::setupAudioEncoder()
{
    // Determine audio codec based on video codec/container
    AVCodecID audio_codec_id;
    int sample_rate = 44100;  // Source audio is 44.1kHz (from TBC/ld-decode)
    int64_t bit_rate = 256000;  // Default bitrate for AAC
    int compression_level = 12;  // For FLAC
    
    // Select audio codec based on video codec
    if (codec_name_ == "ffv1") {
        // FFV1 uses FLAC
        audio_codec_id = AV_CODEC_ID_FLAC;
        sample_rate = 44100;  // Keep original 44.1kHz
    } else if (codec_name_.find("prores") != std::string::npos || 
               codec_name_.find("v210") != std::string::npos ||
               codec_name_.find("v410") != std::string::npos ||
               codec_name_.find("mpeg2video") != std::string::npos) {
        // ProRes, V210, V410, D10 use PCM S24LE
        audio_codec_id = AV_CODEC_ID_PCM_S24LE;
        sample_rate = 44100;  // Keep original 44.1kHz
    } else {
        // H.264, H.265, AV1 use AAC
        audio_codec_id = AV_CODEC_ID_AAC;
        sample_rate = 44100;  // Keep original 44.1kHz
        bit_rate = 256000;
    }
    
    // Find audio encoder
    const AVCodec* audio_codec = avcodec_find_encoder(audio_codec_id);
    if (!audio_codec) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Audio encoder not found for codec ID {}", static_cast<int>(audio_codec_id));
        return false;
    }
    
    // Create audio stream
    audio_stream_ = avformat_new_stream(format_ctx_, nullptr);
    if (!audio_stream_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create audio stream");
        return false;
    }
    audio_stream_->id = format_ctx_->nb_streams - 1;
    
    // Allocate audio codec context
    audio_codec_ctx_ = avcodec_alloc_context3(audio_codec);
    if (!audio_codec_ctx_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate audio codec context");
        return false;
    }
    
    // Configure audio encoder
    audio_codec_ctx_->codec_id = audio_codec_id;
    audio_codec_ctx_->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_codec_ctx_->sample_rate = sample_rate;
    audio_codec_ctx_->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    audio_codec_ctx_->time_base = {1, sample_rate};
    
    // Codec-specific settings
    if (audio_codec_id == AV_CODEC_ID_AAC) {
        audio_codec_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;  // AAC uses planar float
        audio_codec_ctx_->bit_rate = bit_rate;
    } else if (audio_codec_id == AV_CODEC_ID_FLAC) {
        audio_codec_ctx_->sample_fmt = AV_SAMPLE_FMT_S16;  // FLAC uses 16-bit samples
        av_opt_set_int(audio_codec_ctx_->priv_data, "compression_level", compression_level, 0);
    } else if (audio_codec_id == AV_CODEC_ID_PCM_S24LE) {
        audio_codec_ctx_->sample_fmt = AV_SAMPLE_FMT_S32;  // FFmpeg uses S32 for 24-bit PCM
    }
    
    // Open audio encoder
    int ret = avcodec_open2(audio_codec_ctx_, audio_codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to open audio encoder: {}", errbuf);
        return false;
    }
    
    // Copy codec parameters to stream
    ret = avcodec_parameters_from_context(audio_stream_->codecpar, audio_codec_ctx_);
    if (ret < 0) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to copy audio codec parameters");
        return false;
    }
    
    audio_stream_->time_base = audio_codec_ctx_->time_base;
    
    // Allocate audio frame
    audio_frame_ = av_frame_alloc();
    if (!audio_frame_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate audio frame");
        return false;
    }
    
    audio_frame_->format = audio_codec_ctx_->sample_fmt;
    audio_frame_->ch_layout = audio_codec_ctx_->ch_layout;
    audio_frame_->sample_rate = audio_codec_ctx_->sample_rate;
    audio_frame_->nb_samples = audio_codec_ctx_->frame_size ? audio_codec_ctx_->frame_size : 1024;
    
    ret = av_frame_get_buffer(audio_frame_, 0);
    if (ret < 0) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate audio frame buffer");
        return false;
    }
    
    // Allocate audio packet
    audio_packet_ = av_packet_alloc();
    if (!audio_packet_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate audio packet");
        return false;
    }
    
    ORC_LOG_DEBUG("FFmpegOutputBackend: Audio encoder initialized ({} {}kHz stereo)", 
                  audio_codec->name, sample_rate / 1000);
    return true;
}

bool FFmpegOutputBackend::encodeAudioForFrame()
{
    if (!embed_audio_ || !vfr_ || !audio_codec_ctx_) {
        return true;  // No audio to encode
    }
    
    int frame_size = audio_codec_ctx_->frame_size;  // AAC typically uses 1024 samples
    
    // Collect audio samples for these 2 fields and add to persistent buffer
    // NOTE: Padding fields (from field_map) may have no audio samples - we still need
    // to process them to maintain audio/video sync
    for (int field_offset = 0; field_offset < 2 && current_field_for_audio_ < start_field_index_ + num_fields_; field_offset++) {
        auto samples = vfr_->get_audio_samples(FieldID(current_field_for_audio_));
        
        if (samples.empty()) {
            // Padding field with no audio - generate silence to maintain sync
            // Typical field has ~1470 samples for NTSC (48kHz / 29.97 fps / 2 fields)
            // or ~1920 samples for PAL (48kHz / 25 fps / 2 fields)
            uint32_t sample_count = vfr_->get_audio_sample_count(FieldID(current_field_for_audio_));
            if (sample_count == 0) {
                // Estimate based on system if no sample count available
                auto video_params = vfr_->get_video_parameters();
                if (video_params) {
                    // Calculate expected samples per field
                    // Assuming 48kHz audio sample rate
                    double field_rate = (video_params->system == VideoSystem::PAL) ? 50.0 : 59.94;
                    sample_count = static_cast<uint32_t>((48000.0 / field_rate) + 0.5);
                    sample_count *= 2;  // stereo (interleaved L/R pairs)
                }
            }
            // Insert silence (zeros) for padding fields
            samples.resize(sample_count, 0);
        }
        
        audio_buffer_.insert(audio_buffer_.end(), samples.begin(), samples.end());
        current_field_for_audio_++;
    }
    
    ORC_LOG_DEBUG("FFmpegOutputBackend: Audio buffer now has {} int16 values ({} stereo samples)", 
                  audio_buffer_.size(), audio_buffer_.size() / 2);
    
    // Encode audio in chunks of frame_size from the persistent buffer
    while (audio_buffer_.size() >= static_cast<size_t>(frame_size * 2)) {  // *2 for stereo interleaved
        // Convert int16 interleaved PCM to float planar format required by AAC encoder
        av_frame_make_writable(audio_frame_);
        
        float* left_channel = reinterpret_cast<float*>(audio_frame_->data[0]);
        float* right_channel = reinterpret_cast<float*>(audio_frame_->data[1]);
        
        // Convert interleaved samples to planar from start of buffer
        // audio_buffer_ is [L0, R0, L1, R1, L2, R2, ...]
        for (int i = 0; i < frame_size; i++) {
            left_channel[i] = audio_buffer_[i * 2] / 32768.0f;
            right_channel[i] = audio_buffer_[i * 2 + 1] / 32768.0f;
        }
        
        audio_frame_->pts = audio_pts_;
        audio_pts_ += frame_size;
        
        ORC_LOG_DEBUG("FFmpegOutputBackend: Encoding AAC frame with {} samples, pts={}, buffer_remaining={}", 
                     frame_size, audio_frame_->pts, (audio_buffer_.size() / 2) - frame_size);
        
        // Send frame to encoder
        int ret = avcodec_send_frame(audio_codec_ctx_, audio_frame_);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            ORC_LOG_ERROR("FFmpegOutputBackend: Failed to send audio frame: {}", errbuf);
            return false;
        }
        
        // Receive encoded packets
        while (ret >= 0) {
            ret = avcodec_receive_packet(audio_codec_ctx_, audio_packet_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                ORC_LOG_ERROR("FFmpegOutputBackend: Error receiving audio packet: {}", errbuf);
                return false;
            }
            
            // Rescale and write packet
            av_packet_rescale_ts(audio_packet_, audio_codec_ctx_->time_base, audio_stream_->time_base);
            audio_packet_->stream_index = audio_stream_->index;
            
            ret = av_interleaved_write_frame(format_ctx_, audio_packet_);
            av_packet_unref(audio_packet_);
            
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                ORC_LOG_ERROR("FFmpegOutputBackend: Error writing audio packet: {}", errbuf);
                return false;
            }
        }
        
        // Remove the encoded samples from the buffer
        audio_buffer_.erase(audio_buffer_.begin(), audio_buffer_.begin() + frame_size * 2);
    }
    
    return true;
}

bool FFmpegOutputBackend::setupSubtitleEncoder()
{
    // Create subtitle stream for mov_text
    subtitle_stream_ = avformat_new_stream(format_ctx_, nullptr);
    if (!subtitle_stream_) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create subtitle stream");
        return false;
    }
    subtitle_stream_->id = format_ctx_->nb_streams - 1;
    
    // Configure stream parameters directly (mov_text doesn't need a codec context)
    subtitle_stream_->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    subtitle_stream_->codecpar->codec_id = AV_CODEC_ID_MOV_TEXT;
    subtitle_stream_->codecpar->codec_tag = MKTAG('t', 'x', '3', 'g');  // tx3g for MP4
    subtitle_stream_->time_base = time_base_;  // Same as video time base
    
    // For mov_text, we don't need to open an encoder - we write packets directly
    // The codec context is only used as a flag to indicate subtitles are enabled
    subtitle_codec_ctx_ = reinterpret_cast<AVCodecContext*>(1);  // Non-null marker
    
    ORC_LOG_DEBUG("FFmpegOutputBackend: Subtitle stream initialized (mov_text/tx3g for EIA-608)");
    return true;
}

bool FFmpegOutputBackend::encodeClosedCaptionsForFrame()
{
    if (!embed_closed_captions_ || !subtitle_codec_ctx_ || pending_cues_.empty()) {
        return true;  // No captions to encode
    }
    
    // Calculate current frame time in seconds
    // pts_ is in time_base units, convert to seconds
    double frame_time_sec = static_cast<double>(pts_) * time_base_.num / time_base_.den;
    
    // Write all cues that should start at or before this frame
    while (next_cue_index_ < pending_cues_.size()) {
        const auto& cue = pending_cues_[next_cue_index_];
        
        // If this cue starts in the future, we're done for now
        if (cue.start_time > frame_time_sec + 0.1) {  // Small tolerance
            break;
        }
        
        // Create subtitle packet for this cue
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate subtitle packet");
            return false;
        }
        
        // mov_text format: 2-byte big-endian length + UTF-8 text
        std::string text = cue.text;
        size_t text_len = text.length();
        
        if (text_len > 0xFFFF) {
            text_len = 0xFFFF;  // Truncate if too long
            text = text.substr(0, text_len);
        }
        
        size_t packet_size = 2 + text_len;
        av_new_packet(pkt, packet_size);
        
        // Write length prefix (big-endian uint16)
        pkt->data[0] = (text_len >> 8) & 0xFF;
        pkt->data[1] = text_len & 0xFF;
        
        // Copy text
        memcpy(pkt->data + 2, text.c_str(), text_len);
        
        // Convert cue times to time_base units
        int64_t start_pts = static_cast<int64_t>(cue.start_time * subtitle_stream_->time_base.den / subtitle_stream_->time_base.num);
        int64_t end_pts = static_cast<int64_t>(cue.end_time * subtitle_stream_->time_base.den / subtitle_stream_->time_base.num);
        int64_t duration = end_pts - start_pts;
        
        if (duration <= 0) {
            duration = 1;  // Ensure non-zero duration
        }
        
        // Set packet properties
        pkt->stream_index = subtitle_stream_->index;
        pkt->pts = start_pts;
        pkt->dts = start_pts;
        pkt->duration = duration;
        
        ORC_LOG_DEBUG("FFmpegOutputBackend: Writing subtitle cue: start={:.2f}s, end={:.2f}s, duration={}, text='{}'", 
                     cue.start_time, cue.end_time, duration, 
                     text.length() > 50 ? text.substr(0, 50) + "..." : text);
        
        // Write packet
        int ret = av_interleaved_write_frame(format_ctx_, pkt);
        av_packet_free(&pkt);
        
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            ORC_LOG_ERROR("FFmpegOutputBackend: Error writing subtitle packet: {}", errbuf);
            return false;
        }
        
        next_cue_index_++;
    }
    
    return true;
}

} // namespace orc

#endif // HAVE_FFMPEG
