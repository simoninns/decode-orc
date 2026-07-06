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

#include <orc/stage/common_types.h>
#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/eia608_decoder.h>
#include <orc/stage/field_id.h>
#include <orc/stage/frame_id.h>
#include <orc/stage/logging.h>
#include <orc/stage/observers/closed_caption_observer.h>
#include <orc/stage/video_frame_representation.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

#include "componentframe.h"

namespace orc {

namespace {

bool fillAudioFrameFromInterleavedS16(AVFrame* audio_frame,
                                      AVSampleFormat sample_fmt,
                                      const std::vector<int16_t>& audio_buffer,
                                      int frame_size) {
  // audio_buffer is interleaved stereo: [L0, R0, L1, R1, ...]
  switch (sample_fmt) {
    case AV_SAMPLE_FMT_FLTP: {
      float* left_channel = reinterpret_cast<float*>(audio_frame->data[0]);
      float* right_channel = reinterpret_cast<float*>(audio_frame->data[1]);
      for (size_t i = 0; i < static_cast<size_t>(frame_size); i++) {
        left_channel[i] = static_cast<float>(audio_buffer[i * 2]) / 32768.0f;
        right_channel[i] =
            static_cast<float>(audio_buffer[i * 2 + 1]) / 32768.0f;
      }
      return true;
    }
    case AV_SAMPLE_FMT_FLT: {
      float* out = reinterpret_cast<float*>(audio_frame->data[0]);
      for (size_t i = 0; i < static_cast<size_t>(frame_size); i++) {
        out[i * 2] = static_cast<float>(audio_buffer[i * 2]) / 32768.0f;
        out[i * 2 + 1] = static_cast<float>(audio_buffer[i * 2 + 1]) / 32768.0f;
      }
      return true;
    }
    case AV_SAMPLE_FMT_S16: {
      int16_t* out = reinterpret_cast<int16_t*>(audio_frame->data[0]);
      std::copy_n(audio_buffer.begin(), static_cast<size_t>(frame_size) * 2,
                  out);
      return true;
    }
    case AV_SAMPLE_FMT_S16P: {
      int16_t* left_channel = reinterpret_cast<int16_t*>(audio_frame->data[0]);
      int16_t* right_channel = reinterpret_cast<int16_t*>(audio_frame->data[1]);
      for (size_t i = 0; i < static_cast<size_t>(frame_size); i++) {
        left_channel[i] = audio_buffer[i * 2];
        right_channel[i] = audio_buffer[i * 2 + 1];
      }
      return true;
    }
    case AV_SAMPLE_FMT_S32: {
      int32_t* out = reinterpret_cast<int32_t*>(audio_frame->data[0]);
      for (size_t i = 0; i < static_cast<size_t>(frame_size); i++) {
        out[i * 2] = static_cast<int32_t>(audio_buffer[i * 2]) << 16;
        out[i * 2 + 1] = static_cast<int32_t>(audio_buffer[i * 2 + 1]) << 16;
      }
      return true;
    }
    case AV_SAMPLE_FMT_S32P: {
      int32_t* left_channel = reinterpret_cast<int32_t*>(audio_frame->data[0]);
      int32_t* right_channel = reinterpret_cast<int32_t*>(audio_frame->data[1]);
      for (size_t i = 0; i < static_cast<size_t>(frame_size); i++) {
        left_channel[i] = static_cast<int32_t>(audio_buffer[i * 2]) << 16;
        right_channel[i] = static_cast<int32_t>(audio_buffer[i * 2 + 1]) << 16;
      }
      return true;
    }
    default:
      return false;
  }
}

}  // namespace

FFmpegOutputBackend::FFmpegOutputBackend() {}

FFmpegOutputBackend::~FFmpegOutputBackend() { cleanup(); }

void FFmpegOutputBackend::cleanup() {
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

  // subtitle_codec_ctx_ is just a marker (non-null = enabled), not a real
  // context
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

  if (filtered_frame_) {
    av_frame_free(&filtered_frame_);
    filtered_frame_ = nullptr;
  }

  freeFilterGraph();

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

bool FFmpegOutputBackend::initialize(const Configuration& config) {
  last_error_.clear();

  // Parse format string (e.g., "mp4-h264", "mkv-ffv1")
  auto it = config.options.find("format");
  if (it == config.options.end()) {
    ORC_LOG_ERROR("FFmpegOutputBackend: No format specified in options");
    return false;
  }

  std::string format_str = it->second;
  size_t dash_pos = format_str.find('-');
  if (dash_pos == std::string::npos) {
    ORC_LOG_ERROR(
        "FFmpegOutputBackend: Invalid format string '{}' (expected "
        "'container-codec')",
        format_str);
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
    use_lossless =
        (lossless_it->second == "true" || lossless_it->second == "1");
  }

  // Build the video filter chain: optional bwdif deinterlacing followed by
  // the user-supplied filter string (same syntax as FFmpeg's -vf option).
  bool apply_deinterlace = false;
  auto deint_it = config.options.find("apply_deinterlace");
  if (deint_it != config.options.end()) {
    apply_deinterlace = (deint_it->second == "true" || deint_it->second == "1");
  }

  std::string user_filter;
  auto vf_it = config.options.find("video_filter");
  if (vf_it != config.options.end()) {
    user_filter = vf_it->second;
    // Trim surrounding whitespace so "  " behaves like an empty filter.
    const auto first = user_filter.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      user_filter.clear();
    } else {
      const auto last = user_filter.find_last_not_of(" \t\r\n");
      user_filter = user_filter.substr(first, last - first + 1);
    }
  }

  video_filter_desc_.clear();
  if (apply_deinterlace) {
    video_filter_desc_ = "bwdif";
  }
  if (!user_filter.empty()) {
    if (!video_filter_desc_.empty()) {
      video_filter_desc_ += ",";
    }
    video_filter_desc_ += user_filter;
  }

  // Parse the display aspect ratio override ("auto" or "W:H", e.g. "4:3").
  requested_dar_ = {0, 1};
  auto dar_it = config.options.find("display_aspect_ratio");
  if (dar_it != config.options.end() && dar_it->second != "auto" &&
      !dar_it->second.empty()) {
    const std::string& dar_str = dar_it->second;
    const size_t colon = dar_str.find(':');
    int dar_num = 0;
    int dar_den = 0;
    if (colon != std::string::npos) {
      dar_num = std::atoi(dar_str.substr(0, colon).c_str());
      dar_den = std::atoi(dar_str.substr(colon + 1).c_str());
    }
    if (dar_num > 0 && dar_den > 0) {
      requested_dar_ = {dar_num, dar_den};
    } else {
      ORC_LOG_WARN(
          "FFmpegOutputBackend: Ignoring invalid display aspect ratio '{}' "
          "(expected 'W:H', e.g. '4:3')",
          dar_str);
    }
  }

  // Store encoder quality settings
  encoder_preset_ = config.encoder_preset;
  encoder_crf_ = config.encoder_crf;
  encoder_bitrate_ = config.encoder_bitrate;

  // Parse the audio gain ("audio_gain_db", dB) into a linear factor.
  audio_gain_ = 1.0;
  auto gain_it = config.options.find("audio_gain_db");
  if (gain_it != config.options.end() && !gain_it->second.empty()) {
    const double gain_db = std::atof(gain_it->second.c_str());
    if (gain_db != 0.0) {
      audio_gain_ = std::pow(10.0, gain_db / 20.0);
      ORC_LOG_INFO(
          "FFmpegOutputBackend: Applying {} dB audio gain (linear factor "
          "{:.4f})",
          gain_db, audio_gain_);
    }
  }

  // Store audio/subtitle/chapter configuration
  embed_audio_ = config.embed_audio;
  embed_closed_captions_ = config.embed_closed_captions;
  embed_chapter_metadata_ = config.embed_chapter_metadata;
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

  ORC_LOG_DEBUG(
      "FFmpegOutputBackend: Initializing {} output with {} codec (hardware: "
      "{}, lossless: {})",
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
      codec_candidates = {"libx264"};
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
  int ret = avformat_alloc_output_context2(
      &format_ctx_, nullptr, ffmpeg_format.c_str(), config.output_path.c_str());
  if (ret < 0 || !format_ctx_) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate output context: {}",
                  errbuf);
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
    ORC_LOG_ERROR("FFmpegOutputBackend: No suitable {} encoder found",
                  codec_name_);
    ORC_LOG_ERROR("FFmpegOutputBackend: Tried: {}", [&]() {
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
    ORC_LOG_WARN(
        "FFmpegOutputBackend: Audio embedding requested but no audio "
        "available");
    embed_audio_ = false;  // Disable audio
  }

  // Setup subtitle encoder for closed captions if requested
  // Note: Closed captions are only supported in MP4/MOV containers
  if (embed_closed_captions_ && vfr_) {
    if (container_format_ != "mp4" && container_format_ != "mov") {
      ORC_LOG_WARN(
          "FFmpegOutputBackend: Closed captions only supported in MP4/MOV "
          "containers, disabling");
      embed_closed_captions_ = false;
    } else {
      ORC_LOG_DEBUG(
          "FFmpegOutputBackend: Setting up subtitle encoder for closed "
          "captions");
      if (!setupSubtitleEncoder()) {
        ORC_LOG_ERROR("FFmpegOutputBackend: Failed to setup subtitle encoder");
        cleanup();
        return false;
      }

      // Extract closed caption observations from ObservationContext
      ORC_LOG_DEBUG(
          "FFmpegOutputBackend: Extracting closed captions from observation "
          "context");
      eia608_decoder_ = std::make_unique<EIA608Decoder>();

      if (config.observation_context) {
        // Process CC observations for all fields that were processed
        // The FFmpeg sink's trigger() already populated the observation context
        // Use the same field range as audio extraction
        extractClosedCaptionsFromObservations(*config.observation_context,
                                              config.start_field_index,
                                              config.num_fields);
      } else {
        ORC_LOG_WARN(
            "FFmpegOutputBackend: No observation context provided, CC "
            "embedding disabled");
        embed_closed_captions_ = false;
      }
    }
  }

  // Setup chapter metadata from VBI observations if requested
  if (embed_chapter_metadata_) {
    if (container_format_ == "mxf") {
      ORC_LOG_WARN(
          "FFmpegOutputBackend: Chapter metadata not supported in MXF "
          "container, disabling");
      embed_chapter_metadata_ = false;
    } else if (config.observation_context) {
      ORC_LOG_DEBUG(
          "FFmpegOutputBackend: Setting up chapter metadata from VBI "
          "observations");
      setupChapterMetadata(*config.observation_context);
    } else {
      ORC_LOG_WARN(
          "FFmpegOutputBackend: No observation context provided, chapter "
          "metadata disabled");
      embed_chapter_metadata_ = false;
    }
  }

  // Open output file
  ret =
      avio_open(&format_ctx_->pb, config.output_path.c_str(), AVIO_FLAG_WRITE);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to open output file '{}': {}",
                  config.output_path, errbuf);
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

  ORC_LOG_DEBUG("FFmpegOutputBackend: Initialized {} encoder ({}x{})",
                codec_name_, width_, height_);

  return true;
}

bool FFmpegOutputBackend::setupEncoder(const std::string& codec_id,
                                       const orc::SourceParameters& params) {
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
  active_height_ =
      params.last_active_frame_line - params.first_active_frame_line;

  // Store video system, IRE levels, and full parameters for color space
  // configuration.  CVBS_U10_4FSC normative levels from source parameters.
  video_system_ = params.system;
  black_ire_ = static_cast<double>(params.blanking_level);
  white_ire_ = static_cast<double>(params.white_level);
  video_params_ =
      params;  // Store full params for offset handling in convertAndEncode

  // Set source and output dimensions to active area
  src_width_ = active_width_;
  src_height_ = active_height_;

  // Ensure dimensions are even (required by most video codecs, especially
  // H.264/H.265). Match OutputWriter's vertical padding strategy so field order
  // metadata stays consistent with the actual raster layout written to the
  // encoder.
  width_ = (src_width_ + 1) & ~1;    // Round up to even
  height_ = (src_height_ + 1) & ~1;  // Round up to even

  if (width_ != src_width_ || height_ != src_height_) {
    ORC_LOG_DEBUG(
        "FFmpegOutputBackend: Padding dimensions from {}x{} to {}x{} (codecs "
        "require even dimensions)",
        src_width_, src_height_, width_, height_);
  }

  crop_top_ = (height_ > src_height_ && (src_height_ & 1)) ? 1 : 0;

  // The effective display order depends on where the active raster starts
  // within the padded frame, not directly on the video system. This mirrors
  // OutputWriter::getStreamHeader().
  is_tff_ = ((params.first_active_frame_line ^ crop_top_) & 1) == 0;

  // Set codec parameters
  codec_ctx_->codec_id = codec->id;
  codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
  codec_ctx_->width = width_;
  codec_ctx_->height = height_;

  // Set frame rate.
  // PAL-M uses 525-line/59.94 timing (NTSC-like cadence), not PAL 25 fps.
  if (params.system == VideoSystem::NTSC ||
      params.system == VideoSystem::PAL_M) {
    time_base_ = {1001, 30000};  // 29.97 fps
  } else {
    // PAL and unknown systems: default to 25 fps
    time_base_ = {1, 25};
  }

  codec_ctx_->time_base = time_base_;
  codec_ctx_->framerate = av_inv_q(time_base_);
  enc_time_base_ = time_base_;

  // Select pixel format based on what the encoder supports
  // Our source is YUV444P16LE, but most encoders don't support that
  // We'll convert during encoding using swscale
  if (codec_id == "ffv1" || codec_id == "v210") {
    // FFV1/V210: high bit depth 10-bit 4:2:2
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
  } else if (codec_id == "v410") {
    // V410 is 10-bit 4:4:4
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV444P10LE;
  } else if (codec_id == "mpeg2video") {
    // MPEG-2 (D10) uses 8-bit 4:2:2
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV422P;
  } else if (codec_id == "libx264" || codec_id == "libx265") {
    // libx264/libx265 support high quality formats
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV444P;  // 8-bit 4:4:4
  } else if (codec_id.find("_vaapi") != std::string::npos) {
    // VAAPI uses hardware surfaces, but we'll upload from yuv420p
    codec_ctx_->pix_fmt = AV_PIX_FMT_VAAPI;
  } else if (codec_id.find("_qsv") != std::string::npos) {
    // QSV uses hardware surfaces
    codec_ctx_->pix_fmt = AV_PIX_FMT_QSV;
  } else if (codec_id.find("_nvenc") != std::string::npos ||
             codec_id.find("_amf") != std::string::npos) {
    // NVENC and AMD AMF use NV12
    codec_ctx_->pix_fmt = AV_PIX_FMT_NV12;
  } else if (codec_id.find("_videotoolbox") != std::string::npos) {
    // Apple VideoToolbox
    codec_ctx_->pix_fmt = AV_PIX_FMT_VIDEOTOOLBOX;
  } else {
    // AV1 encoders and default fallback
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
    ORC_LOG_DEBUG("FFmpegOutputBackend: Using ProRes profile: {}",
                  prores_profile_);
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
    ORC_LOG_DEBUG("FFmpegOutputBackend: Using ProRes VideoToolbox profile: {}",
                  vt_profile);
  } else if (codec_id == "v210" || codec_id == "v410") {
    // V210/V410 are uncompressed, no special settings needed
    ORC_LOG_DEBUG("FFmpegOutputBackend: Using uncompressed {} codec", codec_id);
  } else if (codec_id == "mpeg2video") {
    // D10 (Sony IMX/XDCAM) settings
    const bool is_625_line_pal = (params.system == VideoSystem::PAL);
    int64_t bitrate =
        is_625_line_pal ? 50000000
                        : 49999840;  // 50 Mbps PAL625, ~50 Mbps NTSC/PAL-M 525
    int bufsize = is_625_line_pal ? 2000000 : 1668328;

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
    codec_ctx_->flags |= AV_CODEC_FLAG_INTERLACED_DCT |
                         AV_CODEC_FLAG_INTERLACED_ME | AV_CODEC_FLAG_LOW_DELAY;

    ORC_LOG_DEBUG("FFmpegOutputBackend: Using D10 settings ({})",
                  is_625_line_pal ? "PAL625" : "525-line");
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
      ORC_LOG_DEBUG("FFmpegOutputBackend: Using bitrate mode: {} bps",
                    encoder_bitrate_);
    } else {
      // CRF mode (constant quality)
      char crf_str[16];
      snprintf(crf_str, sizeof(crf_str), "%d", encoder_crf_);
      av_opt_set(codec_ctx_->priv_data, "crf", crf_str, 0);
      ORC_LOG_DEBUG("FFmpegOutputBackend: Using CRF mode: {}", encoder_crf_);
    }

    // Configure interlaced coding only when the frames reach the encoder
    // unfiltered. With bwdif deinterlacing the output is progressive, and
    // with a custom filter chain the field structure of the output is
    // unknown (e.g. fieldmatch,decimate produces progressive frames), so the
    // per-frame flags from the filter output are used instead.
    if (video_filter_desc_.empty()) {
      if (codec_id == "libx264") {
        av_opt_set(codec_ctx_->priv_data, "x264opts", "interlaced=1", 0);
      } else {
        av_opt_set(codec_ctx_->priv_data, "x265-params", "interlace=true", 0);
      }
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
      ORC_LOG_DEBUG("FFmpegOutputBackend: Using AV1 CRF mode: {}",
                    encoder_crf_);
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
  } else if (codec_id == "h264_videotoolbox" ||
             codec_id == "hevc_videotoolbox") {
    // Apple VideoToolbox settings
    av_opt_set(codec_ctx_->priv_data, "profile", "main", 0);
    av_opt_set_int(codec_ctx_->priv_data, "q", 60, 0);
    stream_->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
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
      av_opt_set(codec_ctx_->priv_data, "quality", "1",
                 0);  // Best quality for VAAPI
    } else if (codec_id.find("_nvenc") != std::string::npos) {
      av_opt_set(codec_ctx_->priv_data, "preset", "hq", 0);
      av_opt_set(codec_ctx_->priv_data, "rc", "vbr", 0);
    }
  }

  // Set up the video filter graph if a chain was requested. This must happen
  // after all codec-specific pixel format adjustments (the buffersink is
  // constrained to the final encoder pixel format) and before the codec is
  // opened (filters may change dimensions, frame rate, and time base, e.g.
  // fieldmatch,decimate or scale).
  if (!video_filter_desc_.empty()) {
    const AVPixFmtDescriptor* fmt_desc =
        av_pix_fmt_desc_get(codec_ctx_->pix_fmt);
    if (fmt_desc && (fmt_desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
      // GPU-surface encoders would need hwupload plumbing inside the graph;
      // fail this candidate so the codec list falls back to a software
      // encoder.
      ORC_LOG_WARN(
          "FFmpegOutputBackend: Video filters are not supported with the "
          "hardware encoder '{}' (GPU surface pixel format); trying software "
          "fallback",
          codec_id);
      avcodec_free_context(&codec_ctx_);
      return false;
    }

    if (!setupFilterGraph()) {
      avcodec_free_context(&codec_ctx_);
      return false;
    }

    // Adopt the filter output geometry and timing for the encoder. Filters
    // such as decimate change the frame rate; scale/crop change dimensions.
    codec_ctx_->width = av_buffersink_get_w(buffersink_ctx_);
    codec_ctx_->height = av_buffersink_get_h(buffersink_ctx_);
    enc_time_base_ = av_buffersink_get_time_base(buffersink_ctx_);
    codec_ctx_->time_base = enc_time_base_;
    const AVRational out_rate = av_buffersink_get_frame_rate(buffersink_ctx_);
    if (out_rate.num > 0 && out_rate.den > 0) {
      codec_ctx_->framerate = out_rate;
    }

    ORC_LOG_INFO(
        "FFmpegOutputBackend: Video filter chain '{}' active ({}x{} -> {}x{}, "
        "output {}/{} fps)",
        video_filter_desc_, width_, height_, codec_ctx_->width,
        codec_ctx_->height, codec_ctx_->framerate.num,
        codec_ctx_->framerate.den);
  }

  // Determine the sample aspect ratio to signal. An explicit display aspect
  // ratio override wins; otherwise adopt whatever the filter chain produced
  // (e.g. via setdar/setsar). {0,1} leaves the SAR unspecified as before.
  sample_aspect_ratio_ = {0, 1};
  if (requested_dar_.num > 0 && requested_dar_.den > 0) {
    // SAR = DAR * height / width, e.g. DAR 4:3 on 928x576 -> SAR 96:203.
    av_reduce(&sample_aspect_ratio_.num, &sample_aspect_ratio_.den,
              static_cast<int64_t>(requested_dar_.num) * codec_ctx_->height,
              static_cast<int64_t>(requested_dar_.den) * codec_ctx_->width,
              static_cast<int64_t>(1024) * 1024);
    ORC_LOG_INFO(
        "FFmpegOutputBackend: Display aspect ratio {}:{} -> sample aspect "
        "ratio {}:{} at {}x{}",
        requested_dar_.num, requested_dar_.den, sample_aspect_ratio_.num,
        sample_aspect_ratio_.den, codec_ctx_->width, codec_ctx_->height);
  } else if (filter_graph_) {
    sample_aspect_ratio_ =
        av_buffersink_get_sample_aspect_ratio(buffersink_ctx_);
  }
  codec_ctx_->sample_aspect_ratio = sample_aspect_ratio_;

  // Color properties for SD systems.
  // PAL-M uses PAL-style decoding in the pipeline, but encoded SD signaling
  // should follow 525-line conventions (same family as NTSC for
  // matrix/primaries tagging).
  if (codec_id.find("264") != std::string::npos ||
      codec_id.find("265") != std::string::npos ||
      codec_id.find("hevc") != std::string::npos) {
    if (params.system == VideoSystem::PAL) {
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
  unsigned int thread_count =
      std::min(std::thread::hardware_concurrency(), 16u);
  if (thread_count == 0) {
    thread_count = 4;  // Fallback if hardware_concurrency returns 0
  }

  codec_ctx_->thread_count = static_cast<int>(thread_count);
  codec_ctx_->thread_type =
      FF_THREAD_FRAME |
      FF_THREAD_SLICE;  // Enable both frame and slice threading

  ORC_LOG_DEBUG(
      "FFmpegOutputBackend: Enabling multi-threaded encoding with {} threads",
      thread_count);

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
    freeFilterGraph();
    return false;
  }

  // Now create the stream (only after codec is successfully opened)
  stream_ = avformat_new_stream(format_ctx_, nullptr);
  if (!stream_) {
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create stream");
    avcodec_free_context(&codec_ctx_);
    freeFilterGraph();
    return false;
  }
  stream_->id = static_cast<int>(format_ctx_->nb_streams) - 1;
  stream_->time_base = enc_time_base_;
  stream_->sample_aspect_ratio = sample_aspect_ratio_;
  // Rate not required for Matroska, but gives nice hints to players
  stream_->avg_frame_rate = codec_ctx_->framerate;
  stream_->r_frame_rate = stream_->avg_frame_rate;

  // Copy codec parameters to stream
  ret = avcodec_parameters_from_context(stream_->codecpar, codec_ctx_);
  if (ret < 0) {
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to copy codec parameters");
    return false;
  }

  if (filter_graph_) {
    // Filtered frames arrive ref-counted from the buffersink; only a shell
    // frame is needed to receive them.
    filtered_frame_ = av_frame_alloc();
    if (!filtered_frame_) {
      ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate filtered frame");
      return false;
    }
  } else {
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
      ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate frame buffers: {}",
                    errbuf);
      return false;
    }
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
    ORC_LOG_ERROR(
        "FFmpegOutputBackend: Failed to allocate source frame buffers: {}",
        errbuf);
    return false;
  }

  if (!filter_graph_) {
    // Initialize swscale context for pixel format conversion with proper
    // color space handling. With a filter graph active the conversion to the
    // encoder pixel format happens inside the graph instead (the buffersink
    // is constrained to the encoder format).
    sws_ctx_ = sws_getContext(
        width_, height_, AV_PIX_FMT_YUV444P16LE,  // Source
        width_, height_, codec_ctx_->pix_fmt,     // Destination
        SWS_LANCZOS, nullptr, nullptr, nullptr    // High quality scaling
    );
    if (!sws_ctx_) {
      ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create swscale context");
      return false;
    }

    // Configure color space conversion
    // ComponentFrame uses IRE scale with cvbs_blanking/cvbs_white range
    // OutputWriter converts this to limited range Y'CbCr (16-235/240 for
    // 8-bit, scaled to 16-bit) So our source is already in "video" range, not
    // full range SWS_CS_ITU601 and SWS_CS_SMPTE170M are both defined as 5 in
    // FFmpeg; they are equivalent for our purposes.
    const int colorspace = SWS_CS_ITU601;

    // Both source and destination are limited (broadcast) range
    const int src_range = 0;  // Limited range (video levels)
    const int dst_range = 0;  // Limited range (broadcast)

    const int* coefficients = sws_getCoefficients(colorspace);

    sws_setColorspaceDetails(sws_ctx_, coefficients, src_range,  // Source
                             coefficients, dst_range,            // Destination
                             0, 1 << 16, 1 << 16);

    ORC_LOG_DEBUG(
        "FFmpegOutputBackend: Configured color conversion: limited→limited "
        "range, colorspace {}",
        video_system_ == VideoSystem::PAL ? "BT.601 (PAL625)"
                                          : "SMPTE170M (525-line)");
  }

  // Allocate packet
  packet_ = av_packet_alloc();
  if (!packet_) {
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate packet");
    return false;
  }

  return true;
}

void FFmpegOutputBackend::freeFilterGraph() {
  if (filter_graph_) {
    avfilter_graph_free(&filter_graph_);
  }
  filter_graph_ = nullptr;
  buffersrc_ctx_ = nullptr;   // Owned by the graph
  buffersink_ctx_ = nullptr;  // Owned by the graph
}

bool FFmpegOutputBackend::setupFilterGraph() {
  freeFilterGraph();

  filter_graph_ = avfilter_graph_alloc();
  if (!filter_graph_) {
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate filter graph");
    return false;
  }

  const AVFilter* buffersrc = avfilter_get_by_name("buffer");
  const AVFilter* buffersink = avfilter_get_by_name("buffersink");
  if (!buffersrc || !buffersink) {
    ORC_LOG_ERROR(
        "FFmpegOutputBackend: buffer/buffersink filters unavailable in this "
        "FFmpeg build");
    freeFilterGraph();
    return false;
  }

  // Describe the frames convertAndEncode() feeds in: padded active-area
  // YUV444P16LE at the source frame rate, square pixels.
  char src_args[256];
  snprintf(src_args, sizeof(src_args),
           "video_size=%dx%d:pix_fmt=%s:time_base=%d/%d:frame_rate=%d/%d:"
           "pixel_aspect=0/1",
           width_, height_, av_get_pix_fmt_name(AV_PIX_FMT_YUV444P16LE),
           time_base_.num, time_base_.den, time_base_.den, time_base_.num);

  int ret = avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                         src_args, nullptr, filter_graph_);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create buffer source: {}",
                  errbuf);
    freeFilterGraph();
    return false;
  }

  ret = avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                     nullptr, nullptr, filter_graph_);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create buffer sink: {}",
                  errbuf);
    freeFilterGraph();
    return false;
  }

  // Force the chain output to the encoder's pixel format by appending a
  // format filter (portable across FFmpeg versions, unlike the buffersink
  // "pix_fmts" option which was deprecated in FFmpeg 7.1).
  const char* sink_fmt_name = av_get_pix_fmt_name(codec_ctx_->pix_fmt);
  if (!sink_fmt_name) {
    ORC_LOG_ERROR("FFmpegOutputBackend: Unknown encoder pixel format {}",
                  static_cast<int>(codec_ctx_->pix_fmt));
    freeFilterGraph();
    return false;
  }
  const std::string full_desc = video_filter_desc_ + ",format=" + sink_fmt_name;

  // Wire buffersrc -> user chain -> format -> buffersink. The chain uses the
  // same textual syntax as FFmpeg's -vf option.
  AVFilterInOut* outputs = avfilter_inout_alloc();
  AVFilterInOut* inputs = avfilter_inout_alloc();
  if (!outputs || !inputs) {
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to allocate filter endpoints");
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    freeFilterGraph();
    return false;
  }

  outputs->name = av_strdup("in");
  outputs->filter_ctx = buffersrc_ctx_;
  outputs->pad_idx = 0;
  outputs->next = nullptr;

  inputs->name = av_strdup("out");
  inputs->filter_ctx = buffersink_ctx_;
  inputs->pad_idx = 0;
  inputs->next = nullptr;

  ret = avfilter_graph_parse_ptr(filter_graph_, full_desc.c_str(), &inputs,
                                 &outputs, nullptr);
  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR("FFmpegOutputBackend: Invalid video filter '{}': {}",
                  video_filter_desc_, errbuf);
    last_error_ =
        "Invalid video filter '" + video_filter_desc_ + "': " + errbuf;
    freeFilterGraph();
    return false;
  }

  ret = avfilter_graph_config(filter_graph_, nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR(
        "FFmpegOutputBackend: Video filter '{}' could not be configured: {}",
        video_filter_desc_, errbuf);
    last_error_ = "Video filter '" + video_filter_desc_ +
                  "' could not be configured: " + errbuf;
    freeFilterGraph();
    return false;
  }

  return true;
}

bool FFmpegOutputBackend::writeFrame(const ::ComponentFrame& component_frame) {
  if (!codec_ctx_ || !src_frame_) {
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

bool FFmpegOutputBackend::convertAndEncode(
    const ComponentFrame& component_frame) {
  // Make source frame writable
  int ret = av_frame_make_writable(src_frame_);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR(
        "FFmpegOutputBackend: Failed to make source frame writable: {}",
        errbuf);
    return false;
  }

  // Copy Y, U, V planes from ComponentFrame to source AVFrame (YUV444P16LE)
  // ComponentFrame uses IRE scale, need to convert to Y'CbCr limited range
  // (BT.601) This matches what OutputWriter::convertLine does for YUV444P16
  // output

  // Constants from outputwriter.cpp
  constexpr double Y_MIN = 1.0 * 256.0;
  constexpr double Y_ZERO = 16.0 * 256.0;    // 4096
  constexpr double Y_SCALE = 219.0 * 256.0;  // 56064
  constexpr double Y_MAX = 254.75 * 256.0;

  constexpr double C_ZERO = 128.0 * 256.0;   // 32768
  constexpr double C_SCALE = 112.0 * 256.0;  // 28672
  constexpr double C_MIN = 1.0 * 256.0;
  constexpr double C_MAX = 254.75 * 256.0;

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
  const int32_t inputLineOffset = video_params_.active_area_cropping_applied
                                      ? 0
                                      : video_params_.first_active_frame_line;
  const int32_t xOffset = video_params_.active_area_cropping_applied
                              ? 0
                              : video_params_.active_video_start;

  // Clear top padding, if any.
  for (int y = 0; y < crop_top_; y++) {
    uint16_t* dst_y = reinterpret_cast<uint16_t*>(src_frame_->data[0] +
                                                  static_cast<ptrdiff_t>(y) *
                                                      src_frame_->linesize[0]);
    uint16_t* dst_u = reinterpret_cast<uint16_t*>(src_frame_->data[1] +
                                                  static_cast<ptrdiff_t>(y) *
                                                      src_frame_->linesize[1]);
    uint16_t* dst_v = reinterpret_cast<uint16_t*>(src_frame_->data[2] +
                                                  static_cast<ptrdiff_t>(y) *
                                                      src_frame_->linesize[2]);

    for (int x = 0; x < width_; x++) {
      dst_y[x] = static_cast<uint16_t>(Y_ZERO);
      dst_u[x] = static_cast<uint16_t>(C_ZERO);
      dst_v[x] = static_cast<uint16_t>(C_ZERO);
    }
  }

  // Copy active video lines from ComponentFrame
  for (int y = 0; y < src_height_; y++) {
    const int32_t inputLine = inputLineOffset + y;
    const double* src_y = component_frame.y(inputLine) + xOffset;
    const double* src_u = component_frame.u(inputLine) + xOffset;
    const double* src_v = component_frame.v(inputLine) + xOffset;

    const int dst_line = crop_top_ + y;
    uint16_t* dst_y = reinterpret_cast<uint16_t*>(
        src_frame_->data[0] +
        static_cast<ptrdiff_t>(dst_line) * src_frame_->linesize[0]);
    uint16_t* dst_u = reinterpret_cast<uint16_t*>(
        src_frame_->data[1] +
        static_cast<ptrdiff_t>(dst_line) * src_frame_->linesize[1]);
    uint16_t* dst_v = reinterpret_cast<uint16_t*>(
        src_frame_->data[2] +
        static_cast<ptrdiff_t>(dst_line) * src_frame_->linesize[2]);

    for (int x = 0; x < src_width_; x++) {
      // Convert Y'UV (IRE scale) to Y'CbCr (limited range) - same as
      // OutputWriter
      dst_y[x] = static_cast<uint16_t>(
          std::clamp(((src_y[x] - yOffset) * yScale) + Y_ZERO, Y_MIN, Y_MAX));
      dst_u[x] = static_cast<uint16_t>(
          std::clamp((src_u[x] * cbScale) + C_ZERO, C_MIN, C_MAX));
      dst_v[x] = static_cast<uint16_t>(
          std::clamp((src_v[x] * crScale) + C_ZERO, C_MIN, C_MAX));
    }

    // Fill padding with black/neutral values if needed
    for (int x = src_width_; x < width_; x++) {
      dst_y[x] = static_cast<uint16_t>(Y_ZERO);  // Black (16*256)
      dst_u[x] = static_cast<uint16_t>(C_ZERO);  // Neutral chroma (128*256)
      dst_v[x] = static_cast<uint16_t>(C_ZERO);
    }
  }

  // Fill bottom padding lines, if needed.
  for (int y = crop_top_ + src_height_; y < height_; y++) {
    uint16_t* dst_y = reinterpret_cast<uint16_t*>(src_frame_->data[0] +
                                                  static_cast<ptrdiff_t>(y) *
                                                      src_frame_->linesize[0]);
    uint16_t* dst_u = reinterpret_cast<uint16_t*>(src_frame_->data[1] +
                                                  static_cast<ptrdiff_t>(y) *
                                                      src_frame_->linesize[1]);
    uint16_t* dst_v = reinterpret_cast<uint16_t*>(src_frame_->data[2] +
                                                  static_cast<ptrdiff_t>(y) *
                                                      src_frame_->linesize[2]);

    for (int x = 0; x < width_; x++) {
      dst_y[x] = static_cast<uint16_t>(Y_ZERO);
      dst_u[x] = static_cast<uint16_t>(C_ZERO);
      dst_v[x] = static_cast<uint16_t>(C_ZERO);
    }
  }

  // Stamp source timing and field structure. pts_ counts source frames in
  // time_base_ units for both the direct and filtered paths.
  src_frame_->pts = pts_++;

  // Signal interlaced field order per-frame (required for correct H.264/H.265
  // SEI Pic Timing, and for field-aware filters such as bwdif/fieldmatch)
  src_frame_->flags |= AV_FRAME_FLAG_INTERLACED;
  if (is_tff_) {
    src_frame_->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
  } else {
    src_frame_->flags &= ~AV_FRAME_FLAG_TOP_FIELD_FIRST;
  }

  if (filter_graph_) {
    // Tag colour properties so auto-inserted conversions inside the graph
    // use the same BT.601 limited-range matrix as the swscale path.
    src_frame_->colorspace = (video_system_ == VideoSystem::PAL)
                                 ? AVCOL_SPC_BT470BG
                                 : AVCOL_SPC_SMPTE170M;
    src_frame_->color_range = AVCOL_RANGE_MPEG;

    ret = av_buffersrc_add_frame_flags(buffersrc_ctx_, src_frame_,
                                       AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      ORC_LOG_ERROR(
          "FFmpegOutputBackend: Failed to feed frame to filter graph: {}",
          errbuf);
      return false;
    }
    return drainFilterGraph();
  }

  // Make destination frame writable before sws_scale writes to it. With
  // FF_THREAD_FRAME enabled, avcodec_send_frame() takes an internal
  // av_frame_ref on frame_, so buf[0]->refcount becomes > 1. Without this call,
  // the next sws_scale would write to a buffer still held by the encoder
  // thread, causing heap corruption (Windows 0xC0000005 inside
  // av_buffer_realloc).
  ret = av_frame_make_writable(frame_);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR(
        "FFmpegOutputBackend: Failed to make destination frame writable: {}",
        errbuf);
    return false;
  }

  // Convert from YUV444P16LE to encoder's pixel format using swscale
  ret = sws_scale(sws_ctx_, src_frame_->data, src_frame_->linesize, 0, height_,
                  frame_->data, frame_->linesize);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to convert pixel format: {}",
                  errbuf);
    return false;
  }

  // Carry timing, aspect, and field structure over to the encoder frame
  frame_->pts = src_frame_->pts;
  frame_->sample_aspect_ratio = sample_aspect_ratio_;
  frame_->flags |= AV_FRAME_FLAG_INTERLACED;
  if (is_tff_) {
    frame_->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
  } else {
    frame_->flags &= ~AV_FRAME_FLAG_TOP_FIELD_FIRST;
  }

  return encodeVideoFrame(frame_);
}

bool FFmpegOutputBackend::encodeVideoFrame(AVFrame* frame) {
  // Send frame to encoder (nullptr flushes)
  int ret = avcodec_send_frame(codec_ctx_, frame);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to send frame to encoder: {}",
                  errbuf);
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

  if (frame) {
    frames_written_++;
  }
  return true;
}

bool FFmpegOutputBackend::drainFilterGraph() {
  while (true) {
    int ret = av_buffersink_get_frame(buffersink_ctx_, filtered_frame_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      // Filter needs more input (e.g. fieldmatch buffering) or has finished.
      return true;
    }
    if (ret < 0) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      ORC_LOG_ERROR(
          "FFmpegOutputBackend: Error pulling frame from filter graph: {}",
          errbuf);
      return false;
    }

    // An explicit display aspect ratio override wins over whatever SAR the
    // filters produced.
    if (sample_aspect_ratio_.num > 0) {
      filtered_frame_->sample_aspect_ratio = sample_aspect_ratio_;
    }

    const bool ok = encodeVideoFrame(filtered_frame_);
    av_frame_unref(filtered_frame_);
    if (!ok) {
      return false;
    }
  }
}

bool FFmpegOutputBackend::finalize() {
  if (!codec_ctx_ || !format_ctx_) {
    return true;  // Already finalized
  }

  // Flush the video filter graph first so frames still buffered inside the
  // filters (e.g. fieldmatch look-ahead) reach the encoder before it is
  // flushed below.
  if (filter_graph_) {
    int flush_ret = av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0);
    if (flush_ret < 0) {
      ORC_LOG_WARN("FFmpegOutputBackend: Error signalling filter graph EOF");
    }
    if (!drainFilterGraph()) {
      ORC_LOG_WARN(
          "FFmpegOutputBackend: Error draining filter graph during finalize");
    }
  }

  // Flush video encoder
  int ret = avcodec_send_frame(codec_ctx_, nullptr);
  if (ret < 0) {
    ORC_LOG_WARN("FFmpegOutputBackend: Error flushing video encoder");
  }

  // Receive remaining video packets
  while (ret >= 0) {
    ret = avcodec_receive_packet(codec_ctx_, packet_);
    if (ret < 0) {
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
      ORC_LOG_DEBUG("FFmpegOutputBackend: Flushing {} remaining audio samples",
                    audio_buffer_.size() / 2);

      // Pad buffer to frame_size if needed
      while (audio_buffer_.size() < static_cast<size_t>(frame_size) * 2) {
        audio_buffer_.push_back(0);  // Pad with silence
      }

      // Encode the final frame
      av_frame_make_writable(audio_frame_);
      if (!fillAudioFrameFromInterleavedS16(audio_frame_,
                                            audio_codec_ctx_->sample_fmt,
                                            audio_buffer_, frame_size)) {
        ORC_LOG_ERROR(
            "FFmpegOutputBackend: Unsupported audio sample format {} during "
            "finalize",
            static_cast<int>(audio_codec_ctx_->sample_fmt));
        cleanup();
        return false;
      }

      audio_frame_->pts = audio_pts_;

      ret = avcodec_send_frame(audio_codec_ctx_, audio_frame_);
      if (ret >= 0) {
        while (ret >= 0) {
          ret = avcodec_receive_packet(audio_codec_ctx_, audio_packet_);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret >= 0) {
            av_packet_rescale_ts(audio_packet_, audio_codec_ctx_->time_base,
                                 audio_stream_->time_base);
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
      if (ret < 0) {
        break;
      }

      av_packet_rescale_ts(audio_packet_, audio_codec_ctx_->time_base,
                           audio_stream_->time_base);
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

std::string FFmpegOutputBackend::getFormatInfo() const {
  std::string info = container_format_ + " (" + codec_name_;
  if (embed_audio_) info += " + audio";
  if (embed_closed_captions_) info += " + CC";
  if (embed_chapter_metadata_) info += " + chapters";
  info += ")";
  return info;
}

bool FFmpegOutputBackend::setupAudioEncoder() {
  // Determine audio codec based on video codec/container
  AVCodecID audio_codec_id;
  int sample_rate = 44100;     // Source audio is 44.1kHz (from TBC/ld-decode)
  int64_t bit_rate = 256000;   // Default bitrate for AAC
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
    ORC_LOG_ERROR(
        "FFmpegOutputBackend: Audio encoder not found for codec ID {}",
        static_cast<int>(audio_codec_id));
    return false;
  }

  // Create audio stream
  audio_stream_ = avformat_new_stream(format_ctx_, nullptr);
  if (!audio_stream_) {
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create audio stream");
    return false;
  }
  audio_stream_->id = static_cast<int>(format_ctx_->nb_streams) - 1;

  // Allocate audio codec context
  audio_codec_ctx_ = avcodec_alloc_context3(audio_codec);
  if (!audio_codec_ctx_) {
    ORC_LOG_ERROR(
        "FFmpegOutputBackend: Failed to allocate audio codec context");
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
    audio_codec_ctx_->sample_fmt =
        AV_SAMPLE_FMT_S16;  // FLAC uses 16-bit samples
    av_opt_set_int(audio_codec_ctx_->priv_data, "compression_level",
                   compression_level, 0);
  } else if (audio_codec_id == AV_CODEC_ID_PCM_S24LE) {
    audio_codec_ctx_->sample_fmt =
        AV_SAMPLE_FMT_S32;  // FFmpeg uses S32 for 24-bit PCM
  }

  // Open audio encoder
  int ret = avcodec_open2(audio_codec_ctx_, audio_codec, nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to open audio encoder: {}",
                  errbuf);
    return false;
  }

  // Copy codec parameters to stream
  ret = avcodec_parameters_from_context(audio_stream_->codecpar,
                                        audio_codec_ctx_);
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
  audio_frame_->nb_samples =
      audio_codec_ctx_->frame_size ? audio_codec_ctx_->frame_size : 1024;

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

  ORC_LOG_DEBUG(
      "FFmpegOutputBackend: Audio encoder initialized ({} {}kHz stereo)",
      audio_codec->name, sample_rate / 1000);
  return true;
}

bool FFmpegOutputBackend::encodeAudioForFrame() {
  if (!embed_audio_ || !vfr_ || !audio_codec_ctx_) {
    return true;  // No audio to encode
  }

  int frame_size =
      audio_codec_ctx_->frame_size;  // AAC typically uses 1024 samples

  // Collect audio samples for this video frame (one frame = two fields).
  // VFrameR provides audio per frame; advance current_field_for_audio_ by 2
  // so the field-based start/num_fields_ accounting stays consistent with the
  // closed-caption extraction that still iterates per field.
  if (current_field_for_audio_ < start_field_index_ + num_fields_) {
    orc::FrameID frame_id = current_field_for_audio_ / 2;
    auto samples = vfr_->get_audio_samples(frame_id);

    if (samples.empty()) {
      // No audio for this frame — generate silence to maintain A/V sync.
      uint32_t sample_count = vfr_->get_audio_sample_count(frame_id);
      if (sample_count == 0) {
        auto video_params = vfr_->get_video_parameters();
        if (video_params) {
          // Estimate expected stereo int16 pairs per frame at 48 kHz.
          double frame_rate =
              (video_params->system == VideoSystem::PAL) ? 25.0 : 29.97;
          sample_count =
              static_cast<uint32_t>(std::lround(48000.0 / frame_rate));
          sample_count *= 2;  // stereo (interleaved L/R pairs)
        }
      }
      samples.resize(sample_count, 0);
    }

    // Apply the configured gain, clipping at full scale. Silence stays
    // silence, so gain-adjusted padding is fine too.
    if (audio_gain_ != 1.0) {
      for (auto& sample : samples) {
        const double scaled = static_cast<double>(sample) * audio_gain_;
        sample = static_cast<int16_t>(
            std::lround(std::clamp(scaled, -32768.0, 32767.0)));
      }
    }

    audio_buffer_.insert(audio_buffer_.end(), samples.begin(), samples.end());
    current_field_for_audio_ += 2;  // Advance by 2 fields (= 1 frame)
  }

  ORC_LOG_DEBUG(
      "FFmpegOutputBackend: Audio buffer now has {} int16 values ({} stereo "
      "samples)",
      audio_buffer_.size(), audio_buffer_.size() / 2);

  // Encode audio in chunks of frame_size from the persistent buffer
  while (audio_buffer_.size() >=
         static_cast<size_t>(frame_size) * 2) {  // *2 for stereo interleaved
    // Convert interleaved int16 PCM to the encoder's required sample format
    av_frame_make_writable(audio_frame_);
    if (!fillAudioFrameFromInterleavedS16(audio_frame_,
                                          audio_codec_ctx_->sample_fmt,
                                          audio_buffer_, frame_size)) {
      ORC_LOG_ERROR("FFmpegOutputBackend: Unsupported audio sample format {}",
                    static_cast<int>(audio_codec_ctx_->sample_fmt));
      return false;
    }

    audio_frame_->pts = audio_pts_;
    audio_pts_ += frame_size;

    ORC_LOG_DEBUG(
        "FFmpegOutputBackend: Encoding audio frame with {} samples, pts={}, "
        "buffer_remaining={}",
        frame_size, audio_frame_->pts, (audio_buffer_.size() / 2) - frame_size);

    // Send frame to encoder
    int ret = avcodec_send_frame(audio_codec_ctx_, audio_frame_);
    if (ret < 0) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      ORC_LOG_ERROR("FFmpegOutputBackend: Failed to send audio frame: {}",
                    errbuf);
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
        ORC_LOG_ERROR("FFmpegOutputBackend: Error receiving audio packet: {}",
                      errbuf);
        return false;
      }

      // Rescale and write packet
      av_packet_rescale_ts(audio_packet_, audio_codec_ctx_->time_base,
                           audio_stream_->time_base);
      audio_packet_->stream_index = audio_stream_->index;

      ret = av_interleaved_write_frame(format_ctx_, audio_packet_);
      av_packet_unref(audio_packet_);

      if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        ORC_LOG_ERROR("FFmpegOutputBackend: Error writing audio packet: {}",
                      errbuf);
        return false;
      }
    }

    // Remove the encoded samples from the buffer
    audio_buffer_.erase(
        audio_buffer_.begin(),
        audio_buffer_.begin() + static_cast<ptrdiff_t>(frame_size) * 2);
  }

  return true;
}

void FFmpegOutputBackend::extractClosedCaptionsFromObservations(
    const IObservationContext& observation_context, uint64_t field_start,
    uint64_t field_count) {
  if (!eia608_decoder_) {
    ORC_LOG_ERROR("FFmpegOutputBackend: EIA-608 decoder not initialized");
    return;
  }

  // Calculate frame rate for timestamp conversion
  double fps = (video_system_ == VideoSystem::NTSC) ? 29.97 : 25.0;

  // Iterate through the field range
  size_t cc_count = 0;
  uint64_t field_end = field_start + field_count;

  for (uint64_t field_num = field_start; field_num < field_end; ++field_num) {
    FieldID field_id(static_cast<uint32_t>(field_num));

    // Check if this field has closed caption data
    auto cc_present =
        observation_context.get(field_id, "closed_caption", "present");
    if (!cc_present || !std::holds_alternative<bool>(*cc_present)) {
      continue;
    }

    if (!std::get<bool>(*cc_present)) {
      continue;  // No CC data in this field
    }

    // Get the CC data bytes
    auto data0_obs =
        observation_context.get(field_id, "closed_caption", "data0");
    auto data1_obs =
        observation_context.get(field_id, "closed_caption", "data1");

    if (!data0_obs || !data1_obs) {
      continue;  // Missing data
    }

    if (!std::holds_alternative<int32_t>(*data0_obs) ||
        !std::holds_alternative<int32_t>(*data1_obs)) {
      continue;  // Wrong data type
    }

    uint8_t data0 = static_cast<uint8_t>(std::get<int32_t>(*data0_obs) &
                                         0x7F);  // Remove parity bit
    uint8_t data1 = static_cast<uint8_t>(std::get<int32_t>(*data1_obs) &
                                         0x7F);  // Remove parity bit

    // Convert field index to timestamp (seconds)
    // Divide by (2 * fps) because there are 2 fields per frame
    double timestamp =
        static_cast<double>(field_num - field_start) / (2.0 * fps);

    // Feed to EIA-608 decoder
    eia608_decoder_->process_bytes(timestamp, data0, data1);
    cc_count++;
  }

  // Finalize decoder and get cues
  double total_duration = static_cast<double>(field_count) / (2.0 * fps);
  pending_cues_ = eia608_decoder_->finalize(total_duration);

  ORC_LOG_INFO(
      "FFmpegOutputBackend: Extracted {} closed caption cues from {} fields "
      "with CC data",
      pending_cues_.size(), cc_count);
}

void FFmpegOutputBackend::setupChapterMetadata(
    const IObservationContext& context) {
  // Collect chapter number transitions from VBI observations
  struct ChapterEntry {
    int32_t number;
    uint64_t field_offset;  // Offset from start_field_index_
  };
  std::vector<ChapterEntry> chapters;
  int32_t current_chapter = -1;

  // Iterate through the same field range used for audio/CC
  for (uint64_t offset = 0; offset <= num_fields_; ++offset) {
    FieldID fid(static_cast<uint32_t>(start_field_index_ + offset));
    auto val = context.get(fid, "vbi", "chapter_number");
    if (val && std::holds_alternative<int32_t>(*val)) {
      int32_t ch = std::get<int32_t>(*val);
      if (ch != current_chapter) {
        chapters.push_back({ch, offset});
        current_chapter = ch;
      }
    }
  }

  if (chapters.empty()) {
    ORC_LOG_DEBUG(
        "FFmpegOutputBackend: No VBI chapter markers found, disabling chapter "
        "metadata");
    embed_chapter_metadata_ = false;
    return;
  }

  ORC_LOG_INFO("FFmpegOutputBackend: Found {} chapter(s) from VBI data",
               chapters.size());

  // Use millisecond precision time base for chapters
  const AVRational chapter_tb = {1, 1000};
  const bool is_ntsc = (video_system_ == VideoSystem::NTSC ||
                        video_system_ == VideoSystem::PAL_M);

  // Convert a field offset (from start_field_index_) to milliseconds
  auto field_offset_to_ms = [&](uint64_t field_offset) -> int64_t {
    if (is_ntsc) {
      // NTSC/PAL-M: 29.97 fps = 59.94 fields/sec → ms = offset * 1001 / 60
      return static_cast<int64_t>(field_offset) * 1001LL / 60LL;
    }
    // PAL: 25 fps = 50 fields/sec → ms = offset * 1000 / 50 = offset * 20
    return static_cast<int64_t>(field_offset) * 20LL;
  };

  const int64_t total_ms = field_offset_to_ms(num_fields_);

  for (size_t i = 0; i < chapters.size(); ++i) {
    const int64_t start_ms = field_offset_to_ms(chapters[i].field_offset);
    const int64_t end_ms =
        (i + 1 < chapters.size())
            ? field_offset_to_ms(chapters[i + 1].field_offset)
            : total_ms;

    const std::string title = "Chapter " + std::to_string(chapters[i].number);

    // Allocate and populate AVChapter manually (avformat_new_chapter was
    // removed in FFmpeg 7.x).
    AVChapter* chapter = static_cast<AVChapter*>(av_mallocz(sizeof(AVChapter)));
    if (!chapter) {
      ORC_LOG_WARN("FFmpegOutputBackend: Failed to allocate chapter {} ('{}')",
                   i + 1, title);
      continue;
    }
    chapter->id = static_cast<int64_t>(i);
    chapter->time_base = chapter_tb;
    chapter->start = start_ms;
    chapter->end = end_ms;
    av_dict_set(&chapter->metadata, "title", title.c_str(), 0);

    // Append chapter pointer to format context chapters array.
    // av_realloc_array expects void*; cast explicitly to satisfy the
    // bugprone-multi-level-implicit-pointer-conversion check.
    AVChapter** new_chapters = static_cast<AVChapter**>(
        av_realloc_array(static_cast<void*>(format_ctx_->chapters),
                         format_ctx_->nb_chapters + 1, sizeof(AVChapter*)));
    if (!new_chapters) {
      ORC_LOG_WARN(
          "FFmpegOutputBackend: Failed to resize chapters array for chapter "
          "{} ('{}')",
          i + 1, title);
      av_dict_free(&chapter->metadata);
      av_free(chapter);
      continue;
    }
    format_ctx_->chapters = new_chapters;
    format_ctx_->chapters[format_ctx_->nb_chapters++] = chapter;

    ORC_LOG_DEBUG("FFmpegOutputBackend: Chapter {}: '{}' ({:.3f}s - {:.3f}s)",
                  i + 1, title, start_ms / 1000.0, end_ms / 1000.0);
  }
}

bool FFmpegOutputBackend::setupSubtitleEncoder() {
  // Create subtitle stream for mov_text
  subtitle_stream_ = avformat_new_stream(format_ctx_, nullptr);
  if (!subtitle_stream_) {
    ORC_LOG_ERROR("FFmpegOutputBackend: Failed to create subtitle stream");
    return false;
  }
  subtitle_stream_->id = static_cast<int>(format_ctx_->nb_streams) - 1;

  // Configure stream parameters directly (mov_text doesn't need a codec
  // context)
  subtitle_stream_->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
  subtitle_stream_->codecpar->codec_id = AV_CODEC_ID_MOV_TEXT;
  subtitle_stream_->codecpar->codec_tag =
      MKTAG('t', 'x', '3', 'g');             // tx3g for MP4
  subtitle_stream_->time_base = time_base_;  // Same as video time base

  // For mov_text, we don't need to open an encoder - we write packets directly
  // The codec context is only used as a flag to indicate subtitles are enabled
  subtitle_codec_ctx_ =
      reinterpret_cast<AVCodecContext*>(1);  // Non-null marker

  ORC_LOG_DEBUG(
      "FFmpegOutputBackend: Subtitle stream initialized (mov_text/tx3g for "
      "EIA-608)");
  return true;
}

bool FFmpegOutputBackend::encodeClosedCaptionsForFrame() {
  if (!embed_closed_captions_ || !subtitle_codec_ctx_ ||
      pending_cues_.empty()) {
    return true;  // No captions to encode
  }

  // Calculate current frame time in seconds
  // pts_ is in time_base units, convert to seconds
  double frame_time_sec =
      static_cast<double>(pts_) * time_base_.num / time_base_.den;

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
    int packet_size_int =
        packet_size > static_cast<size_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(packet_size);
    av_new_packet(pkt, packet_size_int);

    // Write length prefix (big-endian uint16)
    pkt->data[0] = (text_len >> 8) & 0xFF;
    pkt->data[1] = text_len & 0xFF;

    // Copy text
    memcpy(pkt->data + 2, text.c_str(), text_len);

    // Convert cue times to time_base units
    int64_t start_pts =
        static_cast<int64_t>(cue.start_time * subtitle_stream_->time_base.den /
                             subtitle_stream_->time_base.num);
    int64_t end_pts =
        static_cast<int64_t>(cue.end_time * subtitle_stream_->time_base.den /
                             subtitle_stream_->time_base.num);
    int64_t duration = end_pts - start_pts;

    if (duration <= 0) {
      duration = 1;  // Ensure non-zero duration
    }

    // Set packet properties
    pkt->stream_index = subtitle_stream_->index;
    pkt->pts = start_pts;
    pkt->dts = start_pts;
    pkt->duration = duration;

    ORC_LOG_DEBUG(
        "FFmpegOutputBackend: Writing subtitle cue: start={:.2f}s, "
        "end={:.2f}s, duration={}, text='{}'",
        cue.start_time, cue.end_time, duration,
        text.length() > 50 ? text.substr(0, 50) + "..." : text);

    // Write packet
    int ret = av_interleaved_write_frame(format_ctx_, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      ORC_LOG_ERROR("FFmpegOutputBackend: Error writing subtitle packet: {}",
                    errbuf);
      return false;
    }

    next_cue_index_++;
  }

  return true;
}

}  // namespace orc

#endif  // HAVE_FFMPEG
