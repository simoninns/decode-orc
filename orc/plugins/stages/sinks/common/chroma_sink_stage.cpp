/*
 * File:        chroma_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Chroma decoder sink stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "chroma_sink_stage.h"

#include <cvbs_signal_constants.h>

#include "colour_preview_conversion.h"
#include "frame_line_util.h"
#include "logging.h"
#include "preview_helpers.h"
#include "preview_renderer.h"

// Decoder includes (relative to this file)
#include "../../analysis/vectorscope/vectorscope_analysis.h"
#include "decoders/comb.h"
#include "decoders/componentframe.h"
#include "decoders/monodecoder.h"
#include "decoders/outputwriter.h"
#include "decoders/palcolour.h"
#include "decoders/sourcefield.h"
#include "video_parameter_safety.h"

// Output backend includes
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

#include "output_backend.h"

namespace orc {

namespace {

chroma_sink::DecoderVideoProfile decoder_video_profile_for_type(
    const std::string& decoder_type, VideoSystem system) {
  if (decoder_type == "mono") {
    return chroma_sink::DecoderVideoProfile::Mono;
  }

  if (decoder_type == "transform2d" || decoder_type == "transform3d" ||
      decoder_type == "pal2d") {
    return chroma_sink::DecoderVideoProfile::PalColour;
  }

  if (decoder_type == "ntsc1d" || decoder_type == "ntsc2d" ||
      decoder_type == "ntsc3d" || decoder_type == "ntsc3dnoadapt") {
    return chroma_sink::DecoderVideoProfile::NtscColour;
  }

  if (system == VideoSystem::PAL || system == VideoSystem::PAL_M) {
    return chroma_sink::DecoderVideoProfile::PalColour;
  }

  return chroma_sink::DecoderVideoProfile::NtscColour;
}

bool apply_decoder_safe_video_parameters(
    SourceParameters& video_params, chroma_sink::DecoderVideoProfile profile,
    const char* context, std::string* error_message = nullptr) {
  const auto safety =
      chroma_sink::sanitize_video_parameters(video_params, profile);

  if (!safety.warnings.empty()) {
    ORC_LOG_WARN("{}: Adjusted unsafe video parameters: {}", context,
                 chroma_sink::join_issues(safety.warnings));
  }

  if (!safety.ok) {
    const auto joined_errors = chroma_sink::join_issues(safety.errors);
    ORC_LOG_ERROR("{}: Invalid video parameters: {}", context, joined_errors);
    if (error_message != nullptr) {
      *error_message = joined_errors;
    }
    return false;
  }

  video_params = safety.params;
  return true;
}

}  // namespace

// NOTE: ChromaSinkStage is no longer registered as a stage.
// It serves as a base class for RawVideoSinkStage and FFmpegVideoSinkStage.
// Those are the stages users should use.

ChromaSinkStage::ChromaSinkStage()
    : output_path_(""),
      decoder_type_("ntsc2d"),
      output_format_("rgb"),
      chroma_gain_(1.0),
      chroma_phase_(0.0),
      threads_(0)  // 0 means auto-detect
      ,
      luma_nr_(0.0),
      chroma_nr_(0.0),
      ntsc_phase_comp_(true),
      simple_pal_(false),
      transform_threshold_(0.4),
      chroma_weight_(1.0),
      adapt_threshold_(1.0),
      output_padding_(8),
      embed_audio_(false),
      embed_closed_captions_(false),
      embed_chapter_metadata_(false),
      encoder_preset_("medium"),
      encoder_crf_(18),
      encoder_bitrate_(0)  // 0 = use CRF
      ,
      hardware_encoder_("none"),
      prores_profile_("hq"),
      use_lossless_mode_(false),
      apply_deinterlace_(false) {}

ChromaSinkStage::~ChromaSinkStage() {}

std::unique_ptr<MonoDecoder> ChromaSinkStage::create_yc_mono_decoder(
    const orc::SourceParameters& videoParams) const {
  MonoDecoder::MonoConfiguration config;
  config.yNRLevel = luma_nr_;
  config.filterChroma = false;  // Luma-only input contains no chroma to filter.
  config.videoParameters = videoParams;
  return std::make_unique<MonoDecoder>(config);
}

NodeTypeInfo ChromaSinkStage::get_node_type_info() const {
  // This should never be called since ChromaSinkStage is not registered.
  // It's kept for compatibility as a base class.
  return NodeTypeInfo{
      NodeType::SINK,
      "chroma_sink_base",
      "Chroma Sink Base Class",
      "Internal base class - use Raw Video Sink or FFmpeg Video Sink instead.",
      1,  // min_inputs
      1,  // max_inputs
      0,  // min_outputs
      0,  // max_outputs
      VideoFormatCompatibility::ALL,
      SinkCategory::CORE,
      "Sink (Core)"};
}

std::vector<ArtifactPtr> ChromaSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context [[maybe_unused]]) {
  // Apply parameters so decoder_type_ and other settings reflect the current
  // project before the preview is rendered. Without this, the stage retains its
  // constructor default (ntsc2d) when a PAL project is first loaded, causing
  // the chroma preview to use the wrong decoder.
  if (!parameters.empty()) {
    set_parameters(parameters);
  }

  // ChromaSinkStage is primarily a video output sink and does not extract
  // observations. Observations are collected by the LD sink or dedicated
  // analysis stages. Cache input for preview rendering (thread-safe)
  if (!inputs.empty()) {
    std::lock_guard<std::mutex> lock(cached_input_mutex_);
    cached_input_ =
        std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
            inputs[0]);
  }

  // Sink stages don't produce outputs during normal execution
  // They are triggered manually to write data
  ORC_LOG_DEBUG(
      "ChromaSink execute called on instance {} (cached input for preview)",
      static_cast<void*>(this));
  return {};  // No outputs
}

std::vector<ParameterDescriptor> ChromaSinkStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType source_type) const {
  // Determine available decoder types based on project video format
  std::vector<std::string> decoder_options;
  std::string decoder_default = "ntsc2d";

  if (project_format == VideoSystem::PAL ||
      project_format == VideoSystem::PAL_M) {
    decoder_options = {"pal2d", "transform2d", "transform3d", "mono"};
    decoder_default = "pal2d";
  } else if (project_format == VideoSystem::NTSC) {
    decoder_options = {"ntsc1d", "ntsc2d", "ntsc3d", "ntsc3dnoadapt", "mono"};
    decoder_default = "ntsc2d";
  } else {
    // Unknown system - show all options
    decoder_options = {"pal2d",  "transform2d", "transform3d",   "ntsc1d",
                       "ntsc2d", "ntsc3d",      "ntsc3dnoadapt", "mono"};
    decoder_default = "ntsc2d";
  }

  std::string decoder_description =
      "Chroma decoder to use: pal2d, transform2d, transform3d, ntsc1d, ntsc2d, "
      "ntsc3d, ntsc3dnoadapt, mono";
  if (source_type == SourceType::YC) {
    decoder_description +=
        "\n"
        "YC sources: transform2d/transform3d are not compatible and will fall "
        "back to pal2d.";
  }

  std::vector<ParameterDescriptor> params = {
      ParameterDescriptor{
          "output_path", "Output Path",
          "Path to output file (RGB, YUV, or Y4M format based on "
          "output_format)",
          ParameterType::FILE_PATH,
          ParameterConstraints{std::nullopt,
                               std::nullopt,
                               std::string(""),
                               {},
                               false,
                               std::nullopt},
          ".rgb|.yuv|.y4m|.mp4|.mkv"  // file_extension_hint - multiple options
      },
      ParameterDescriptor{
          "decoder_type",
          "Decoder Type",
          decoder_description,
          ParameterType::STRING,
          {{}, {}, decoder_default, decoder_options, false, std::nullopt}},
      ParameterDescriptor{
          "output_format",
          "Output Format",
          "Output format:\n"
          "  Raw: rgb (RGB48), yuv (YUV444P16), y4m (YUV444P16 with Y4M "
          "headers)\n"
          "  Encoded: mp4-h264, mkv-ffv1 (requires FFmpeg libraries)",
          ParameterType::STRING,
          {{},
           {},
           std::string("rgb"),
           OutputBackendFactory::getSupportedFormats(),
           false,
           std::nullopt}},
      ParameterDescriptor{"chroma_gain",
                          "Chroma Gain",
                          "Gain factor applied to chroma components (color "
                          "saturation). Range: 0.0-10.0",
                          ParameterType::DOUBLE,
                          {0.0, 10.0, 1.0, {}, false, std::nullopt}},
      ParameterDescriptor{"chroma_phase",
                          "Chroma Phase",
                          "Phase rotation applied to chroma components in "
                          "degrees. Range: -180 to 180",
                          ParameterType::DOUBLE,
                          {-180.0, 180.0, 0.0, {}, false, std::nullopt}},
      ParameterDescriptor{
          "luma_nr",
          "Luma Noise Reduction",
          "Luma noise reduction level in dB. 0 = disabled. Range: 0.0-10.0",
          ParameterType::DOUBLE,
          {0.0, 10.0, 0.0, {}, false, std::nullopt}},
      ParameterDescriptor{"chroma_nr",
                          "Chroma Noise Reduction",
                          "Chroma noise reduction level in dB (NTSC only). 0 = "
                          "disabled. Range: 0.0-10.0",
                          ParameterType::DOUBLE,
                          {0.0, 10.0, 0.0, {}, false, std::nullopt}},
      ParameterDescriptor{"output_padding",
                          "Output Padding",
                          "Pad output to multiple of this many pixels on both "
                          "axes. Range: 1-32",
                          ParameterType::INT32,
                          {1, 32, 8, {}, false, std::nullopt}},
      ParameterDescriptor{
          "encoder_preset",
          "Encoder Preset",
          "Encoder speed/quality preset (for H.264/H.265): fast, medium, slow, "
          "veryslow",
          ParameterType::STRING,
          {{},
           {},
           std::string("medium"),
           {"fast", "medium", "slow", "veryslow"},
           false,
           ParameterDependency{"output_format", {"mp4-h264", "mkv-ffv1"}}}},
      ParameterDescriptor{
          "encoder_crf",
          "Encoder CRF",
          "Constant Rate Factor for quality (0-51, lower=better). Typical: "
          "18-28. 0 = use bitrate instead",
          ParameterType::INT32,
          {0,
           51,
           18,
           {},
           false,
           ParameterDependency{"output_format", {"mp4-h264", "mkv-ffv1"}}}},
      ParameterDescriptor{
          "encoder_bitrate",
          "Encoder Bitrate",
          "Target bitrate in bits/sec (0 = use CRF instead). Example: 10000000 "
          "= 10 Mbps",
          ParameterType::INT32,
          {0,
           100000000,
           0,
           {},
           false,
           ParameterDependency{"output_format", {"mp4-h264", "mkv-ffv1"}}}},
      ParameterDescriptor{
          "hardware_encoder",
          "Hardware Encoder",
          "Use hardware accelerated encoding (none, vaapi, nvenc, qsv, amf, "
          "videotoolbox). Auto-detection in preset dialog.",
          ParameterType::STRING,
          {{},
           {},
           std::string("none"),
           {"none", "vaapi", "nvenc", "qsv", "amf", "videotoolbox"},
           false,
           ParameterDependency{
               "output_format",
               {"mp4-h264", "mp4-hevc", "mov-h264", "mov-hevc"}}}},
      ParameterDescriptor{
          "prores_profile",
          "ProRes Profile",
          "ProRes quality profile: proxy, lt, standard, hq (default), 4444, "
          "4444xq",
          ParameterType::STRING,
          {{},
           {},
           std::string("hq"),
           {"proxy", "lt", "standard", "hq", "4444", "4444xq"},
           false,
           ParameterDependency{"output_format", {"mov-prores"}}}},
      ParameterDescriptor{
          "use_lossless_mode",
          "Use Lossless Mode",
          "Enable mathematically lossless encoding (H.264/H.265/AV1 only, "
          "overrides CRF)",
          ParameterType::BOOL,
          {{},
           {},
           false,
           {},
           false,
           ParameterDependency{
               "output_format",
               {"mp4-h264", "mp4-hevc", "mp4-av1", "mov-h264", "mov-hevc"}}}},
      ParameterDescriptor{
          "apply_deinterlace",
          "Apply Deinterlacing",
          "Apply bwdif deinterlacing filter for progressive web playback",
          ParameterType::BOOL,
          {{},
           {},
           false,
           {},
           false,
           ParameterDependency{
               "output_format",
               {"mp4-h264", "mp4-hevc", "mp4-av1", "mov-h264", "mov-hevc"}}}},
      ParameterDescriptor{
          "embed_audio",
          "Embed Analogue Audio",
          "Embed analogue audio in output file (requires audio in source, "
          "MP4/MKV only)",
          ParameterType::BOOL,
          {{},
           {},
           false,
           {},
           false,
           ParameterDependency{"output_format", {"mp4-h264", "mkv-ffv1"}}}},
      ParameterDescriptor{
          "embed_closed_captions",
          "Embed Closed Captions",
          "Embed closed captions as mov_text subtitles (converts EIA-608 to "
          "text, MP4/MOV only)",
          ParameterType::BOOL,
          {{},
           {},
           false,
           {},
           false,
           ParameterDependency{
               "output_format",
               {"mp4-h264", "mp4-hevc", "mp4-av1", "mov-h264", "mov-hevc"}}}},
      ParameterDescriptor{
          "embed_chapter_metadata",
          "Embed Chapter Metadata",
          "Write chapter markers from VBI data to output file (MKV/MP4/MOV "
          "only; requires chapter numbers decoded by BiphaseObserver)",
          ParameterType::BOOL,
          {{},
           {},
           false,
           {},
           false,
           ParameterDependency{
               "output_format",
               {"mkv-ffv1", "mov-prores", "mov-v210", "mov-v410", "mp4-h264",
                "mov-h264", "mp4-hevc", "mov-hevc", "mp4-av1"}}}}};

  // Add format-specific parameters
  if (project_format == VideoSystem::NTSC) {
    params.push_back(ParameterDescriptor{
        "ntsc_phase_comp",
        "NTSC Phase Compensation",
        "Adjust phase per-line using burst phase (NTSC only)",
        ParameterType::BOOL,
        {{}, {}, true, {}, false, std::nullopt}});
    params.push_back(ParameterDescriptor{
        "chroma_weight",
        "Chroma Weight",
        "Chroma weight for 3D adaptive filter (NTSC 3D only). Higher = prefer "
        "more 2D result. Range: 0.0-10.0",
        ParameterType::DOUBLE,
        {0.0,
         10.0,
         1.0,
         {},
         false,
         ParameterDependency{"decoder_type", {"ntsc3d", "ntsc3dnoadapt"}}}});
    params.push_back(
        ParameterDescriptor{"adapt_threshold",
                            "Adapt Threshold",
                            "3D adaptive filter threshold (NTSC 3D only). "
                            "Higher = prefer more 3D result. Range: 0.0-10.0",
                            ParameterType::DOUBLE,
                            {0.0,
                             10.0,
                             1.0,
                             {},
                             false,
                             ParameterDependency{"decoder_type", {"ntsc3d"}}}});
  } else if (project_format == VideoSystem::PAL ||
             project_format == VideoSystem::PAL_M) {
    params.push_back(ParameterDescriptor{
        "simple_pal",
        "Simple PAL",
        "Use 1D UV filter for Transform PAL (simpler, faster, lower quality)",
        ParameterType::BOOL,
        {{},
         {},
         false,
         {},
         false,
         ParameterDependency{"decoder_type", {"transform2d", "transform3d"}}}});
    params.push_back(ParameterDescriptor{
        "transform_threshold",
        "Transform Threshold",
        "Similarity threshold for Transform PAL decoder (default 0.4). Higher "
        "= more transform filtering applied. Range: 0.0-1.0",
        ParameterType::DOUBLE,
        {0.0,
         1.0,
         0.4,
         {},
         false,
         ParameterDependency{"decoder_type", {"transform2d", "transform3d"}}}});
  } else {
    // Unknown format - include both for backwards compatibility
    params.push_back(ParameterDescriptor{
        "ntsc_phase_comp",
        "NTSC Phase Compensation",
        "Adjust phase per-line using burst phase (NTSC only)",
        ParameterType::BOOL,
        {{}, {}, true, {}, false, std::nullopt}});
    params.push_back(ParameterDescriptor{
        "chroma_weight",
        "Chroma Weight",
        "Chroma weight for 3D adaptive filter (NTSC 3D only). Higher = prefer "
        "more 2D result. Range: 0.0-10.0",
        ParameterType::DOUBLE,
        {0.0,
         10.0,
         1.0,
         {},
         false,
         ParameterDependency{"decoder_type", {"ntsc3d", "ntsc3dnoadapt"}}}});
    params.push_back(
        ParameterDescriptor{"adapt_threshold",
                            "Adapt Threshold",
                            "3D adaptive filter threshold (NTSC 3D only). "
                            "Higher = prefer more 3D result. Range: 0.0-10.0",
                            ParameterType::DOUBLE,
                            {0.0,
                             10.0,
                             1.0,
                             {},
                             false,
                             ParameterDependency{"decoder_type", {"ntsc3d"}}}});
    params.push_back(ParameterDescriptor{
        "simple_pal",
        "Simple PAL",
        "Use 1D UV filter for Transform PAL (simpler, faster, lower quality)",
        ParameterType::BOOL,
        {{},
         {},
         false,
         {},
         false,
         ParameterDependency{"decoder_type", {"transform2d", "transform3d"}}}});
    params.push_back(ParameterDescriptor{
        "transform_threshold",
        "Transform Threshold",
        "Similarity threshold for Transform PAL decoder (default 0.4). Higher "
        "= more transform filtering applied. Range: 0.0-1.0",
        ParameterType::DOUBLE,
        {0.0,
         1.0,
         0.4,
         {},
         false,
         ParameterDependency{"decoder_type", {"transform2d", "transform3d"}}}});
  }

  return params;
}

std::map<std::string, ParameterValue> ChromaSinkStage::get_parameters() const {
  std::map<std::string, ParameterValue> params;
  params["output_path"] = output_path_;
  params["decoder_type"] = decoder_type_;
  params["output_format"] = output_format_;
  params["chroma_gain"] = chroma_gain_;
  params["chroma_phase"] = chroma_phase_;
  params["luma_nr"] = luma_nr_;
  params["chroma_nr"] = chroma_nr_;
  params["ntsc_phase_comp"] = ntsc_phase_comp_;
  params["simple_pal"] = simple_pal_;
  params["transform_threshold"] = transform_threshold_;
  params["chroma_weight"] = chroma_weight_;
  params["adapt_threshold"] = adapt_threshold_;
  params["output_padding"] = output_padding_;
  params["encoder_preset"] = encoder_preset_;
  params["encoder_crf"] = encoder_crf_;
  params["encoder_bitrate"] = encoder_bitrate_;
  params["embed_audio"] = embed_audio_;
  params["embed_closed_captions"] = embed_closed_captions_;
  params["embed_chapter_metadata"] = embed_chapter_metadata_;
  params["hardware_encoder"] = hardware_encoder_;
  params["prores_profile"] = prores_profile_;
  params["use_lossless_mode"] = use_lossless_mode_;
  params["apply_deinterlace"] = apply_deinterlace_;
  return params;
}

bool ChromaSinkStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  bool decoder_config_changed = false;

  for (const auto& [key, value] : params) {
    if (key == "output_path") {
      if (std::holds_alternative<std::string>(value)) {
        output_path_ = std::get<std::string>(value);
      }
    } else if (key == "decoder_type") {
      if (std::holds_alternative<std::string>(value)) {
        auto new_val = std::get<std::string>(value);
        if (new_val == "auto") {
          ORC_LOG_WARN(
              "ChromaSink: decoder_type 'auto' is no longer supported (loaded "
              "from old project). Migrating to 'ntsc2d'.");
          new_val = "ntsc2d";
        }
        if (new_val != decoder_type_) {
          ORC_LOG_DEBUG("ChromaSink: decoder_type changed from '{}' to '{}'",
                        decoder_type_, new_val);
          decoder_type_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "output_format") {
      if (std::holds_alternative<std::string>(value)) {
        output_format_ = std::get<std::string>(value);
      }
    } else if (key == "chroma_gain") {
      if (std::holds_alternative<double>(value)) {
        auto new_val = std::get<double>(value);
        if (new_val != chroma_gain_) {
          ORC_LOG_DEBUG("ChromaSink: chroma_gain changed from {} to {}",
                        chroma_gain_, new_val);
          chroma_gain_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "chroma_phase") {
      if (std::holds_alternative<double>(value)) {
        auto new_val = std::get<double>(value);
        if (new_val != chroma_phase_) {
          ORC_LOG_DEBUG("ChromaSink: chroma_phase changed from {} to {}",
                        chroma_phase_, new_val);
          chroma_phase_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "luma_nr") {
      if (std::holds_alternative<double>(value)) {
        auto new_val = std::get<double>(value);
        if (new_val != luma_nr_) {
          ORC_LOG_DEBUG("ChromaSink: luma_nr changed from {} to {}", luma_nr_,
                        new_val);
          luma_nr_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "chroma_nr") {
      if (std::holds_alternative<double>(value)) {
        auto new_val = std::get<double>(value);
        if (new_val != chroma_nr_) {
          ORC_LOG_DEBUG("ChromaSink: chroma_nr changed from {} to {}",
                        chroma_nr_, new_val);
          chroma_nr_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "ntsc_phase_comp") {
      if (std::holds_alternative<bool>(value)) {
        auto new_val = std::get<bool>(value);
        if (new_val != ntsc_phase_comp_) {
          ORC_LOG_DEBUG("ChromaSink: ntsc_phase_comp changed from {} to {}",
                        ntsc_phase_comp_, new_val);
          ntsc_phase_comp_ = new_val;
          decoder_config_changed = true;
        }
      } else if (std::holds_alternative<std::string>(value)) {
        // Handle string representation of boolean (from YAML parsing)
        auto str_val = std::get<std::string>(value);
        bool new_val =
            (str_val == "true" || str_val == "1" || str_val == "yes");
        if (new_val != ntsc_phase_comp_) {
          ORC_LOG_DEBUG(
              "ChromaSink: ntsc_phase_comp changed from {} to {} (from string "
              "'{}')",
              ntsc_phase_comp_, new_val, str_val);
          ntsc_phase_comp_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "chroma_weight") {
      if (std::holds_alternative<double>(value)) {
        auto new_val = std::get<double>(value);
        if (new_val != chroma_weight_) {
          ORC_LOG_DEBUG("ChromaSink: chroma_weight changed from {} to {}",
                        chroma_weight_, new_val);
          chroma_weight_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "adapt_threshold") {
      if (std::holds_alternative<double>(value)) {
        auto new_val = std::get<double>(value);
        if (new_val != adapt_threshold_) {
          ORC_LOG_DEBUG("ChromaSink: adapt_threshold changed from {} to {}",
                        adapt_threshold_, new_val);
          adapt_threshold_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "simple_pal") {
      if (std::holds_alternative<bool>(value)) {
        auto new_val = std::get<bool>(value);
        if (new_val != simple_pal_) {
          ORC_LOG_DEBUG("ChromaSink: simple_pal changed from {} to {}",
                        simple_pal_, new_val);
          simple_pal_ = new_val;
          decoder_config_changed = true;
        }
      } else if (std::holds_alternative<std::string>(value)) {
        // Handle string representation of boolean (from YAML parsing)
        auto str_val = std::get<std::string>(value);
        bool new_val =
            (str_val == "true" || str_val == "1" || str_val == "yes");
        if (new_val != simple_pal_) {
          ORC_LOG_DEBUG(
              "ChromaSink: simple_pal changed from {} to {} (from string '{}')",
              simple_pal_, new_val, str_val);
          simple_pal_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "transform_threshold") {
      if (std::holds_alternative<double>(value)) {
        auto new_val = std::get<double>(value);
        if (new_val != transform_threshold_) {
          ORC_LOG_DEBUG("ChromaSink: transform_threshold changed from {} to {}",
                        transform_threshold_, new_val);
          transform_threshold_ = new_val;
          decoder_config_changed = true;
        }
      }
    } else if (key == "output_padding") {
      if (std::holds_alternative<int>(value)) {
        output_padding_ = std::get<int>(value);
      }
    } else if (key == "encoder_preset") {
      if (std::holds_alternative<std::string>(value)) {
        encoder_preset_ = std::get<std::string>(value);
      }
    } else if (key == "encoder_crf") {
      if (std::holds_alternative<int>(value)) {
        encoder_crf_ = std::get<int>(value);
      }
    } else if (key == "encoder_bitrate") {
      if (std::holds_alternative<int>(value)) {
        encoder_bitrate_ = std::get<int>(value);
      }
    } else if (key == "embed_audio") {
      if (std::holds_alternative<bool>(value)) {
        embed_audio_ = std::get<bool>(value);
      } else if (std::holds_alternative<std::string>(value)) {
        auto str_val = std::get<std::string>(value);
        embed_audio_ =
            (str_val == "true" || str_val == "1" || str_val == "yes");
      }
    } else if (key == "embed_closed_captions") {
      if (std::holds_alternative<bool>(value)) {
        embed_closed_captions_ = std::get<bool>(value);
      } else if (std::holds_alternative<std::string>(value)) {
        auto str_val = std::get<std::string>(value);
        embed_closed_captions_ =
            (str_val == "true" || str_val == "1" || str_val == "yes");
      }
    } else if (key == "embed_chapter_metadata") {
      if (std::holds_alternative<bool>(value)) {
        embed_chapter_metadata_ = std::get<bool>(value);
      } else if (std::holds_alternative<std::string>(value)) {
        auto str_val = std::get<std::string>(value);
        embed_chapter_metadata_ =
            (str_val == "true" || str_val == "1" || str_val == "yes");
      }
    } else if (key == "hardware_encoder") {
      if (std::holds_alternative<std::string>(value)) {
        hardware_encoder_ = std::get<std::string>(value);
      }
    } else if (key == "prores_profile") {
      if (std::holds_alternative<std::string>(value)) {
        prores_profile_ = std::get<std::string>(value);
      }
    } else if (key == "use_lossless_mode") {
      if (std::holds_alternative<bool>(value)) {
        use_lossless_mode_ = std::get<bool>(value);
      } else if (std::holds_alternative<std::string>(value)) {
        auto str_val = std::get<std::string>(value);
        use_lossless_mode_ =
            (str_val == "true" || str_val == "1" || str_val == "yes");
      }
    } else if (key == "apply_deinterlace") {
      if (std::holds_alternative<bool>(value)) {
        apply_deinterlace_ = std::get<bool>(value);
      } else if (std::holds_alternative<std::string>(value)) {
        auto str_val = std::get<std::string>(value);
        apply_deinterlace_ =
            (str_val == "true" || str_val == "1" || str_val == "yes");
      }
    }
  }

  // Log if decoder configuration was changed
  if (decoder_config_changed) {
    ORC_LOG_DEBUG(
        "ChromaSink: Decoder configuration changed - cached decoder will be "
        "recreated on next preview");
  }

  return true;
}

bool ChromaSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context [[maybe_unused]]) {
  // ChromaSinkStage is a video output sink and does not require or populate
  // observations. The LD sink and dedicated analysis stages handle observation
  // extraction.
  ORC_LOG_DEBUG("ChromaSink: Trigger called - starting decode");

  // Mark trigger as in progress and reset cancel flag
  trigger_in_progress_.store(true);
  cancel_requested_.store(false);

  // Apply any parameter updates
  set_parameters(parameters);

  // Validate output path is set
  if (output_path_.empty()) {
    ORC_LOG_ERROR("ChromaSink: No output path specified");
    trigger_status_ = "Error: No output path specified";
    trigger_in_progress_.store(false);
    return false;
  }

  // 1. Extract VideoFrameRepresentation from input
  if (inputs.empty()) {
    ORC_LOG_ERROR("ChromaSink: No input provided");
    trigger_status_ = "Error: No input";
    trigger_in_progress_.store(false);
    return false;
  }

  auto vfr =
      std::dynamic_pointer_cast<orc::VideoFrameRepresentation>(inputs[0]);
  if (!vfr) {
    ORC_LOG_ERROR("ChromaSink: Input is not a VideoFrameRepresentation");
    trigger_status_ = "Error: Invalid input type";
    trigger_in_progress_.store(false);
    return false;
  }

  // 2. Get video parameters from VFR
  auto video_params_opt = vfr->get_video_parameters();
  if (!video_params_opt) {
    ORC_LOG_ERROR("ChromaSink: Input has no video parameters");
    trigger_status_ = "Error: No video parameters";
    trigger_in_progress_.store(false);
    return false;
  }

  // 3. Use orc-core SourceParameters directly
  auto videoParams = *video_params_opt;  // Make a copy so we can modify it

  ORC_LOG_DEBUG("ChromaSink: Video parameters from source metadata:");
  ORC_LOG_DEBUG("  Horizontal active region: {} to {} ({} samples)",
                videoParams.active_video_start, videoParams.active_video_end,
                videoParams.active_video_end - videoParams.active_video_start);
  ORC_LOG_DEBUG(
      "  Vertical active region: {} to {} ({} lines)",
      videoParams.first_active_frame_line, videoParams.last_active_frame_line,
      videoParams.last_active_frame_line - videoParams.first_active_frame_line);

  // Apply line parameter overrides from hints
  // Hints provide both frame-based and field-based values
  // Use frame-based values for video output (matches decoder requirements)
  auto active_line_hint = vfr->get_active_line_hint();
  if (active_line_hint && active_line_hint->is_valid()) {
    videoParams.first_active_frame_line =
        active_line_hint->first_active_frame_line;
    videoParams.last_active_frame_line =
        active_line_hint->last_active_frame_line;
    ORC_LOG_DEBUG(
        "ChromaSink: Using active line hint: frame first={}, last={} (field "
        "first={}, last={})",
        active_line_hint->first_active_frame_line,
        active_line_hint->last_active_frame_line,
        active_line_hint->first_active_field_line,
        active_line_hint->last_active_field_line);
  } else {
    ORC_LOG_DEBUG(
        "ChromaSink: No active line hint available, using metadata defaults");
  }

  // Apply padding adjustments to active video region BEFORE configuring decoder
  // This ensures the decoder processes the correct region that will be written
  // to output
  {
    OutputWriter::Configuration writerConfig;
    writerConfig.paddingAmount = output_padding_;

    ORC_LOG_DEBUG(
        "ChromaSink: BEFORE padding adjustment: first_active_frame_line={}, "
        "last_active_frame_line={} (paddingAmount={})",
        videoParams.first_active_frame_line, videoParams.last_active_frame_line,
        writerConfig.paddingAmount);

    // Create temporary output writer just to apply padding adjustments
    OutputWriter tempWriter;
    tempWriter.updateConfiguration(videoParams, writerConfig);
    // videoParams now has adjusted activeVideoStart/End values

    ORC_LOG_DEBUG(
        "ChromaSink: AFTER padding adjustment: first_active_frame_line={}, "
        "last_active_frame_line={}",
        videoParams.first_active_frame_line,
        videoParams.last_active_frame_line);
  }

  // Use active area boundaries from video parameters (metadata/hints)
  // Set flag so decoders know to use relative indexing when writing to
  // ComponentFrame
  videoParams.active_area_cropping_applied = true;

  ORC_LOG_DEBUG(
      "ChromaSink: Using active area from video parameters: {}x{}",
      videoParams.active_video_end - videoParams.active_video_start,
      videoParams.last_active_frame_line - videoParams.first_active_frame_line);

  // 4. Create appropriate decoder
  // Note: We'll use the decoder classes directly (synchronously)
  // without the threading infrastructure for now

  std::unique_ptr<MonoDecoder> monoDecoder;
  std::unique_ptr<MonoDecoder> ycMonoDecoder;
  std::unique_ptr<PalColour> palDecoder;
  std::unique_ptr<Comb> ntscDecoder;

  const bool is_yc_source = vfr->has_separate_channels();
  const bool isPal = (videoParams.system == orc::VideoSystem::PAL ||
                      videoParams.system == orc::VideoSystem::PAL_M);

  bool useMonoDecoder = (decoder_type_ == "mono");
  bool usePalDecoder =
      (decoder_type_ == "pal2d" || decoder_type_ == "transform2d" ||
       decoder_type_ == "transform3d");
  bool useNtscDecoder = (decoder_type_.find("ntsc") == 0);

  std::string parameter_error;
  const auto decoder_profile =
      decoder_video_profile_for_type(decoder_type_, videoParams.system);
  if (!apply_decoder_safe_video_parameters(videoParams, decoder_profile,
                                           "ChromaSink trigger",
                                           &parameter_error)) {
    trigger_status_ = "Error: Invalid video parameters - " + parameter_error;
    trigger_in_progress_.store(false);
    return false;
  }

  if (useMonoDecoder) {
    MonoDecoder::MonoConfiguration config;
    config.yNRLevel = luma_nr_;
    config.filterChroma = false;  // Mono decoder doesn't need comb filtering
    config.videoParameters = videoParams;
    monoDecoder = std::make_unique<MonoDecoder>(config);
    ORC_LOG_DEBUG("ChromaSink: Using decoder: mono");
  } else if (usePalDecoder) {
    // Check if we're trying to use Transform PAL filters with YC sources
    bool isTransformFilter =
        (decoder_type_ == "transform2d" || decoder_type_ == "transform3d");

    if (is_yc_source && isTransformFilter) {
      ORC_LOG_ERROR(
          "ChromaSink: Transform PAL filters (transform2d/transform3d) are not "
          "compatible with YC sources.");
      ORC_LOG_ERROR(
          "ChromaSink: YC sources have already-separated Y and C channels and "
          "do not need frequency-domain filtering.");
      ORC_LOG_ERROR(
          "ChromaSink: Please use 'pal2d' or 'mono' decoder instead.");
      ORC_LOG_ERROR(
          "ChromaSink: Falling back to 'pal2d' decoder for YC source.");
      // Override to pal2d for YC sources
      decoder_type_ = "pal2d";
    }

    PalColour::Configuration config;
    config.chromaGain = chroma_gain_;
    config.chromaPhase = chroma_phase_;
    config.yNRLevel = luma_nr_;
    config.simplePAL = simple_pal_;
    config.transformThreshold = transform_threshold_;
    config.showFFTs = false;

    // Set filter mode based on decoder type
    std::string filterName;
    if (decoder_type_ == "transform3d") {
      config.chromaFilter = PalColour::transform3DFilter;
      filterName = "transform3d";
    } else if (decoder_type_ == "transform2d") {
      config.chromaFilter = PalColour::transform2DFilter;
      filterName = "transform2d";
    } else if (decoder_type_ == "pal2d") {
      // pal2d uses the basic PAL colour filter (default)
      config.chromaFilter = PalColour::palColourFilter;
      filterName = "pal2d";
    } else {
      config.chromaFilter = PalColour::palColourFilter;
      filterName = "pal2d (default)";
    }

    palDecoder = std::make_unique<PalColour>();
    palDecoder->updateConfiguration(videoParams, config);
    ORC_LOG_DEBUG("ChromaSink: Using decoder: {} (PAL)", filterName);
  } else if (useNtscDecoder) {
    Comb::Configuration config;
    config.chromaGain = chroma_gain_;
    config.chromaPhase = chroma_phase_;
    config.cNRLevel = chroma_nr_;
    config.yNRLevel = luma_nr_;
    config.phaseCompensation = ntsc_phase_comp_;
    config.chromaWeight = chroma_weight_;
    config.adaptThreshold = adapt_threshold_;
    config.showMap = false;

    // Set dimensions based on decoder type
    std::string decoderName;
    if (decoder_type_ == "ntsc1d") {
      config.dimensions = 1;
      config.adaptive = false;
      decoderName = "ntsc1d";
    } else if (decoder_type_ == "ntsc3d") {
      config.dimensions = 3;
      config.adaptive = true;
      decoderName = "ntsc3d";
    } else if (decoder_type_ == "ntsc3dnoadapt") {
      config.dimensions = 3;
      config.adaptive = false;
      decoderName = "ntsc3dnoadapt";
    } else {
      config.dimensions = 2;
      config.adaptive = false;
      decoderName = "ntsc2d";
    }

    ntscDecoder = std::make_unique<Comb>();
    ntscDecoder->updateConfiguration(videoParams, config);
    ORC_LOG_DEBUG("ChromaSink: Using decoder: {} (NTSC)", decoderName);
  } else {
    ORC_LOG_ERROR("ChromaSink: Unknown decoder type: {}", decoder_type_);
    trigger_status_ = "Error: Unknown decoder type";
    trigger_in_progress_.store(false);
    return false;
  }

  if (is_yc_source && !useMonoDecoder) {
    ycMonoDecoder = create_yc_mono_decoder(videoParams);
    ORC_LOG_DEBUG(
        "ChromaSink: YC source dual-decode enabled (mono Y route + colour UV "
        "route)");
  }

  // 5. Determine frame range to process.
  // Use the frame_range from VFrameR (may be filtered by upstream stages like
  // field_map). FrameIDRange is inclusive on both ends [first, last].
  orc::FrameIDRange frame_range = vfr->frame_range();
  size_t total_source_frames = vfr->frame_count();

  // Convert inclusive [first, last] to exclusive end [start, end).
  size_t start_frame = frame_range.first;
  size_t end_frame = frame_range.last + 1;  // exclusive

  ORC_LOG_DEBUG(
      "ChromaSink: Processing frames {} to {} (of {} in source, frame range "
      "{}-{})",
      start_frame + 1, end_frame, total_source_frames, frame_range.first,
      frame_range.last);

  // 6. Field ordering and interlacing structure
  // In interlaced video, each frame consists of two fields captured
  // sequentially. Fields are stored in chronological order: 0, 1, 2, 3, 4, 5...
  //
  // Field parity is assigned based on field index:
  //   - Even field indices (0, 2, 4...) → FieldParity::Top    → first field
  //   - Odd field indices (1, 3, 5...)  → FieldParity::Bottom → second field
  //
  // This relationship is consistent across both NTSC and PAL systems.
  // Frame N (1-based) consists of fields (2*N-2, 2*N-1) in 0-based indexing.

  // 6. Determine decoder lookbehind/lookahead requirements.
  // For YC dual-decode, this is always driven by the colour route decoder.
  // The mono Y route is decoded over the same extended range only to keep
  // frame/field indexing aligned for merge_luma_from().
  const bool use_yc_dual_decode = (ycMonoDecoder != nullptr);
  int32_t colourLookBehindFrames = 0;
  int32_t colourLookAheadFrames = 0;

  if (palDecoder) {
    // PalColour internally uses Transform3D which needs lookbehind/lookahead
    if (decoder_type_ == "transform3d" || decoder_type_ == "transform2d") {
      // Design §8.7: VFrameR frame-based architecture; Transform3D returns 1
      // frame of look-behind and 4 frames of look-ahead.
      colourLookBehindFrames = (decoder_type_ == "transform3d") ? 1 : 0;
      colourLookAheadFrames = (decoder_type_ == "transform3d") ? 4 : 0;
    }
  } else if (ntscDecoder) {
    // NTSC 3D decoder might need lookbehind/lookahead
    if (decoder_type_ == "ntsc3d" || decoder_type_ == "ntsc3dnoadapt") {
      colourLookBehindFrames = 1;  // From Comb::Configuration::getLookBehind()
      colourLookAheadFrames = 2;   // From Comb::Configuration::getLookAhead()
    }
  }

  int32_t lookBehindFrames = colourLookBehindFrames;
  int32_t lookAheadFrames = colourLookAheadFrames;

  ORC_LOG_DEBUG(
      "ChromaSink: Decoder requires lookBehind={} frames, lookAhead={} frames",
      lookBehindFrames, lookAheadFrames);
  if (use_yc_dual_decode) {
    ORC_LOG_DEBUG(
        "ChromaSink: YC dual decode lookaround is colour-route driven; Y-route "
        "reuses the same extended range for alignment");
  }

  // 7. Calculate extended frame range including lookbehind/lookahead
  // Note: extended_start_frame can be negative (will use black padding)
  int32_t extended_start_frame =
      static_cast<int32_t>(start_frame) - lookBehindFrames;
  int32_t extended_end_frame =
      static_cast<int32_t>(end_frame) + lookAheadFrames;

  // 8. Build the frame info list — one entry per frame including look-around.
  // Frame data is loaded on-demand in worker threads.
  struct FrameInfo {
    orc::FrameID frame_id;
    bool use_blank;
  };

  std::vector<FrameInfo> frameInfoList;
  frameInfoList.reserve(
      static_cast<size_t>(extended_end_frame - extended_start_frame));

  ORC_LOG_DEBUG(
      "ChromaSink: Preparing {} frame descriptors (frames {}-{}) for decode",
      extended_end_frame - extended_start_frame, extended_start_frame + 1,
      extended_end_frame);

  for (int32_t frame = extended_start_frame; frame < extended_end_frame;
       frame++) {
    bool useBlankFrame =
        (frame < 0) || (static_cast<orc::FrameID>(frame) < frame_range.first) ||
        (static_cast<orc::FrameID>(frame) > frame_range.last);

    orc::FrameID fid = useBlankFrame ? 0 : static_cast<orc::FrameID>(frame);
    if (!useBlankFrame && !vfr->has_frame(fid)) {
      ORC_LOG_WARN("ChromaSink: Skipping frame {} (not present in VFrameR)",
                   frame + 1);
      useBlankFrame = true;
    }
    frameInfoList.push_back({fid, useBlankFrame});
  }

  // 10. Process frames in parallel using worker threads
  // CRITICAL: Transform3D is a 3D temporal FFT filter that processes frames at
  // specific Z-positions (temporal indices). Each frame MUST be at the SAME
  // Z-position (field indices lookBehind*2 to lookBehind*2+2) regardless of its
  // frame number, otherwise the FFT results will differ. Workers process frames
  // independently with proper context.
  //
  // THREAD SAFETY: Each worker thread creates its own decoder instance to avoid
  // state conflicts. Transform PAL decoders use FFT buffers that cannot be
  // shared between threads.

  // Calculate how many frames to OUTPUT
  // field_range represents the actual fields to output (already filtered by
  // upstream stages) We output all these frames - lookahead is only for decoder
  // context (extended_range handles that)
  int32_t numOutputFrames = static_cast<int32_t>(end_frame - start_frame);
  int32_t numFrames = numOutputFrames;

  ORC_LOG_DEBUG("ChromaSink: Will output {} frames from field range {}-{}",
                numOutputFrames, field_range.start.value(),
                field_range.end.value());

  // Initialize output backend BEFORE decoding to enable streaming writes
  auto backend = OutputBackendFactory::create(output_format_);
  if (!backend) {
    ORC_LOG_ERROR("ChromaSink: Unknown or unsupported output format: {}",
                  output_format_);
    trigger_status_ = "Error: Unknown format '" + output_format_ + "'";
    trigger_in_progress_.store(false);
    return false;
  }

  OutputBackend::Configuration backendConfig;
  backendConfig.output_path = output_path_;
  backendConfig.video_params = videoParams;
  backendConfig.padding_amount = output_padding_;
  backendConfig.options["format"] = output_format_;
  backendConfig.encoder_preset = encoder_preset_;
  backendConfig.encoder_crf = encoder_crf_;
  backendConfig.encoder_bitrate = encoder_bitrate_;
  backendConfig.embed_audio = embed_audio_;
  backendConfig.embed_closed_captions = embed_closed_captions_;
  backendConfig.embed_chapter_metadata = embed_chapter_metadata_;
  backendConfig.options["hardware_encoder"] = hardware_encoder_;
  backendConfig.options["prores_profile"] = prores_profile_;
  backendConfig.options["use_lossless_mode"] =
      use_lossless_mode_ ? "true" : "false";
  backendConfig.options["apply_deinterlace"] =
      apply_deinterlace_ ? "true" : "false";
  backendConfig.observation_context = &observation_context;

  // Set field-equivalent range for audio, closed caption, and/or chapter
  // metadata extraction. The ffmpeg backend uses field-based indexing
  // internally; convert frame range to field units (1 frame = 2 fields).
  if ((embed_audio_ && vfr && vfr->has_audio()) || embed_closed_captions_ ||
      embed_chapter_metadata_) {
    backendConfig.start_field_index = frame_range.first * 2;
    backendConfig.num_fields = (frame_range.last - frame_range.first + 1) * 2;

    if (embed_audio_ && vfr && vfr->has_audio()) {
      backendConfig.vfr = vfr.get();
      ORC_LOG_DEBUG(
          "ChromaSink: Audio embedding enabled (frames {} to {} = {} frames, "
          "{} field-equiv)",
          frame_range.first, frame_range.last, numOutputFrames,
          backendConfig.num_fields);
    }

    if (embed_closed_captions_) {
      ORC_LOG_DEBUG(
          "ChromaSink: Closed caption embedding enabled (frames {} to {} = "
          "{} frames)",
          frame_range.first, frame_range.last, numOutputFrames);
    }

    if (embed_chapter_metadata_) {
      ORC_LOG_DEBUG(
          "ChromaSink: Chapter metadata embedding enabled (frames {} to {} = "
          "{} frames)",
          frame_range.first, frame_range.last, numOutputFrames);
    }
  }

  if (!backend->initialize(backendConfig)) {
    ORC_LOG_ERROR("ChromaSink: Failed to initialize {} output backend",
                  output_format_);
    trigger_status_ =
        "Error: Failed to initialize " + output_format_ + " output";
    trigger_in_progress_.store(false);
    return false;
  }

  ORC_LOG_DEBUG("ChromaSink: Streaming {} frames to {}", numOutputFrames,
                backend->getFormatInfo());

  // Use vector of optional frames to track which have been written
  // This allows out-of-order completion while maintaining sequential writes
  std::vector<std::optional<::ComponentFrame>> outputFrames;
  outputFrames.resize(numOutputFrames);

  // Determine number of threads to use
  int32_t numThreads = threads_;
  if (numThreads <= 0) {
    numThreads = static_cast<int32_t>(std::thread::hardware_concurrency());
    if (numThreads <= 0) numThreads = 4;  // Fallback
  }
  // Don't use more threads than frames
  numThreads = std::min(numThreads, numFrames);

  ORC_LOG_DEBUG("ChromaSink: Processing {} frames using {} worker threads",
                numFrames, numThreads);

  // Start timing for performance measurement
  auto decode_start_time = std::chrono::high_resolution_clock::now();

  // Report initial progress
  if (progress_callback_) {
    progress_callback_(0, numFrames, "Starting decoding...");
  }

  // Shared state for work distribution and output writing
  std::atomic<int32_t> nextFrameIdx{0};
  std::atomic<bool> abortFlag{false};
  std::atomic<int32_t> completedFrames{0};
  std::atomic<int32_t> nextFrameToWrite{0};
  std::mutex outputMutex;  // Protects backend writes and frame buffering

  // CRITICAL: FFTW plan creation with FFTW_MEASURE is NOT thread-safe
  // (see FFTW docs: http://www.fftw.org/fftw3_doc/Thread-safety.html)
  // We must serialize all decoder instantiations that create FFTW plans
  std::mutex fftwPlanMutex;

  // Worker thread function - each worker creates its own decoder instance
  auto workerFunc = [&]() {
    // Create thread-local decoder instance
    std::unique_ptr<MonoDecoder> threadMonoDecoder;
    std::unique_ptr<MonoDecoder> threadYcMonoDecoder;
    std::unique_ptr<PalColour> threadPalDecoder;
    std::unique_ptr<Comb> threadNtscDecoder;

    if (monoDecoder) {
      // Clone configuration from main decoder
      MonoDecoder::MonoConfiguration config;
      config.yNRLevel = luma_nr_;
      config.filterChroma = false;
      config.videoParameters = videoParams;
      threadMonoDecoder = std::make_unique<MonoDecoder>(config);
    } else if (palDecoder) {
      // Clone configuration from main decoder
      PalColour::Configuration config;
      config.chromaGain = chroma_gain_;
      config.chromaPhase = chroma_phase_;
      config.yNRLevel = luma_nr_;
      config.simplePAL = simple_pal_;
      config.transformThreshold = transform_threshold_;
      config.showFFTs = false;

      if (decoder_type_ == "transform3d") {
        config.chromaFilter = PalColour::transform3DFilter;
      } else if (decoder_type_ == "transform2d") {
        config.chromaFilter = PalColour::transform2DFilter;
      } else {
        config.chromaFilter = PalColour::palColourFilter;
      }

      // CRITICAL: Protect FFTW plan creation (Transform PAL uses FFTW_MEASURE
      // which is not thread-safe)
      {
        std::lock_guard<std::mutex> lock(fftwPlanMutex);
        threadPalDecoder = std::make_unique<PalColour>();
        threadPalDecoder->updateConfiguration(videoParams, config);
      }
    } else if (ntscDecoder) {
      // Clone configuration from main decoder
      Comb::Configuration config;
      config.chromaGain = chroma_gain_;
      config.chromaPhase = chroma_phase_;
      config.cNRLevel = chroma_nr_;
      config.yNRLevel = luma_nr_;
      config.phaseCompensation = ntsc_phase_comp_;
      config.chromaWeight = chroma_weight_;
      config.adaptThreshold = adapt_threshold_;
      config.showMap = false;

      if (decoder_type_ == "ntsc1d") {
        config.dimensions = 1;
        config.adaptive = false;
      } else if (decoder_type_ == "ntsc3d") {
        config.dimensions = 3;
        config.adaptive = true;
      } else if (decoder_type_ == "ntsc3dnoadapt") {
        config.dimensions = 3;
        config.adaptive = false;
      } else {
        config.dimensions = 2;
        config.adaptive = false;
      }

      threadNtscDecoder = std::make_unique<Comb>();
      threadNtscDecoder->updateConfiguration(videoParams, config);
    }

    if (ycMonoDecoder) {
      threadYcMonoDecoder = create_yc_mono_decoder(videoParams);
    }

    while (!abortFlag) {
      // Check for cancellation
      if (cancel_requested_.load()) {
        abortFlag.store(true);
        break;
      }

      // Get next frame to process
      int32_t frameIdx = nextFrameIdx.fetch_add(1);
      if (frameIdx >= numFrames) {
        break;  // No more frames to process
      }

      // Build a field array for this ONE frame by loading data on-demand.
      // [lookbehind fields... target frame fields... lookahead fields...]
      // frameInfoList has one entry per frame; we expand each to 2
      // SourceFields.
      std::vector<SourceField> frameFields;

      // Owned black buffers for blank SourceFields; must outlive frameFields.
      // Use a deque so that push_back never invalidates existing data()
      // pointers.
      std::deque<std::vector<int16_t>> ownedBlankData;

      // The actual frame number we're processing
      int32_t actualFrameNum = static_cast<int32_t>(start_frame) + frameIdx;

      // Position in frameInfoList where this frame's entry is
      int32_t frameStartIdx = (actualFrameNum - extended_start_frame);

      // Calculate the range to load: lookbehind + target + lookahead (in
      // frames)
      int32_t copyStartIdx = frameStartIdx - lookBehindFrames;
      int32_t copyEndIdx = frameStartIdx + 1 + lookAheadFrames;

      // Clamp to valid range
      copyStartIdx = std::max(0, copyStartIdx);
      copyEndIdx =
          std::min(static_cast<int32_t>(frameInfoList.size()), copyEndIdx);

      // Blanking level for black fields (CVBS 10-bit domain)
      const int16_t blankingLevel =
          isPal ? static_cast<int16_t>(orc::kPalBlanking)
                : static_cast<int16_t>(orc::kNtscBlanking);

      // Helper: compute sample count for field 1 or 2 given video params
      auto fieldSampleCount = [&](bool is_first) -> size_t {
        if (isPal) {
          // EBU Tech. 3280-E §1.3.1: field 1 = 311×1135 + 2×1136, field 2 =
          // 310×1135 + 2×1136
          return is_first ? (311 * 1135 + 2 * 1136) : (310 * 1135 + 2 * 1136);
        }
        // NTSC / PAL_M: uniform sampling
        size_t spl = (videoParams.system == orc::VideoSystem::PAL_M)
                         ? static_cast<size_t>(orc::kPalMSamplesPerLine)
                         : static_cast<size_t>(orc::kNtscSamplesPerLine);
        size_t field_lines = is_first
                                 ? static_cast<size_t>(orc::kNtscField1Lines)
                                 : static_cast<size_t>(orc::kNtscFrameLines -
                                                       orc::kNtscField1Lines);
        return spl * field_lines;
      };

      // Helper: build a blank SourceField backed by an entry in ownedBlankData
      auto makeBlankField = [&](bool is_first_field) -> SourceField {
        size_t samples = fieldSampleCount(is_first_field);
        ownedBlankData.emplace_back(samples, blankingLevel);
        const int16_t* buf = ownedBlankData.back().data();

        SourceField sf;
        sf.is_first_field = is_first_field;
        sf.data = buf;
        sf.is_yc = is_yc_source;
        if (is_yc_source) {
          sf.luma_data = buf;
          sf.chroma_data = buf;
        }

        if (isPal) {
          sf.line_count = is_first_field
                              ? static_cast<size_t>(orc::kPalField1Lines)
                              : static_cast<size_t>(orc::kPalFrameLines -
                                                    orc::kPalField1Lines);
          sf.samples_per_line = 1135;
          sf.line_ptrs.reserve(sf.line_count);
          const size_t blank_field_base =
              is_first_field ? 0 : static_cast<size_t>(orc::kPalField1Lines);
          size_t offset = 0;
          for (size_t ln = 0; ln < sf.line_count; ++ln) {
            sf.line_ptrs.push_back(buf + offset);
            offset += frame_line_sample_count(
                orc::VideoSystem::PAL,
                static_cast<size_t>(orc::kPalMaxSamplesPerLine - 1),
                blank_field_base + ln);
          }
          if (is_yc_source) {
            sf.luma_line_ptrs = sf.line_ptrs;
            sf.chroma_line_ptrs = sf.line_ptrs;
          }
        } else {
          size_t spl = (videoParams.system == orc::VideoSystem::PAL_M)
                           ? static_cast<size_t>(orc::kPalMSamplesPerLine)
                           : static_cast<size_t>(orc::kNtscSamplesPerLine);
          sf.line_count = is_first_field
                              ? static_cast<size_t>(orc::kNtscField1Lines)
                              : static_cast<size_t>(orc::kNtscFrameLines -
                                                    orc::kNtscField1Lines);
          sf.samples_per_line = spl;
          // NTSC: uniform lines, no line_ptrs needed
        }
        return sf;
      };

      for (int32_t i = copyStartIdx; i < copyEndIdx; i++) {
        const auto& fi = frameInfoList[i];

        if (fi.use_blank) {
          frameFields.push_back(makeBlankField(true));
          frameFields.push_back(makeBlankField(false));
        } else {
          frameFields.push_back(
              convertToSourceField(vfr.get(), fi.frame_id, true, videoParams));
          frameFields.push_back(
              convertToSourceField(vfr.get(), fi.frame_id, false, videoParams));
        }
      }

      // The target frame's position within frameFields (in field units)
      int32_t actualLookbehindFields = (frameStartIdx - copyStartIdx) * 2;

      // CRITICAL: For Transform3D temporal consistency, all frames must be
      // decoded at the SAME Z-position (temporal index) regardless of their
      // frame number. Always decode at lookBehindFrames * 2 field indices.
      // If we don't have full lookbehind (edge frames), pad with black fields.
      std::vector<SourceField> paddedFrameFields;
      int32_t requiredLookbehindFields = lookBehindFrames * 2;

      if (actualLookbehindFields < requiredLookbehindFields) {
        int32_t paddingNeeded =
            requiredLookbehindFields - actualLookbehindFields;

        // Create black fields for padding (always in field pairs)
        for (int32_t p = 0; p < paddingNeeded; p++) {
          paddedFrameFields.push_back(makeBlankField(p % 2 == 0));
        }

        for (const auto& field : frameFields) {
          paddedFrameFields.push_back(field);
        }

        frameFields = std::move(paddedFrameFields);
      }

      // Now all frames decode at the same Z-position: after lookBehindFrames *
      // 2 fields
      int32_t frameStartIndex = requiredLookbehindFields;
      int32_t frameEndIndex = frameStartIndex + 2;

      // Prepare single-frame output buffer
      std::vector<::ComponentFrame> singleOutput;
      singleOutput.resize(1);

      // Decode this ONE frame using thread-local decoder
      if (threadYcMonoDecoder) {
        std::vector<SourceField> ycYFields;
        std::vector<SourceField> ycCFields;
        ycYFields.reserve(frameFields.size());
        ycCFields.reserve(frameFields.size());

        for (const auto& field : frameFields) {
          // Y-route: composite view of luma channel only
          SourceField yField = field;
          yField.is_yc = false;
          yField.data = field.luma_data;
          yField.luma_data = nullptr;
          yField.chroma_data = nullptr;
          yField.line_ptrs = field.luma_line_ptrs;
          yField.luma_line_ptrs.clear();
          yField.chroma_line_ptrs.clear();
          ycYFields.push_back(std::move(yField));

          // C-route: composite view of chroma channel only
          SourceField cField = field;
          cField.is_yc = false;
          cField.data = field.chroma_data;
          cField.luma_data = nullptr;
          cField.chroma_data = nullptr;
          cField.line_ptrs = field.chroma_line_ptrs;
          cField.luma_line_ptrs.clear();
          cField.chroma_line_ptrs.clear();
          ycCFields.push_back(std::move(cField));
        }

        std::vector<::ComponentFrame> yFrames(1);
        std::vector<::ComponentFrame> uvFrames(1);

        threadYcMonoDecoder->decodeFrames(ycYFields, frameStartIndex,
                                          frameEndIndex, yFrames);

        if (threadPalDecoder) {
          threadPalDecoder->decodeFrames(ycCFields, frameStartIndex,
                                         frameEndIndex, uvFrames);
        } else if (threadNtscDecoder) {
          threadNtscDecoder->decodeFrames(ycCFields, frameStartIndex,
                                          frameEndIndex, uvFrames);
        }

        uvFrames[0].merge_luma_from(yFrames[0]);
        singleOutput[0] = std::move(uvFrames[0]);
      } else if (threadMonoDecoder) {
        threadMonoDecoder->decodeFrames(frameFields, frameStartIndex,
                                        frameEndIndex, singleOutput);
      } else if (threadPalDecoder) {
        threadPalDecoder->decodeFrames(frameFields, frameStartIndex,
                                       frameEndIndex, singleOutput);
      } else if (threadNtscDecoder) {
        threadNtscDecoder->decodeFrames(frameFields, frameStartIndex,
                                        frameEndIndex, singleOutput);
      }

      // Store the result in the buffer
      {
        std::lock_guard<std::mutex> lock(outputMutex);
        outputFrames[frameIdx] = singleOutput[0];

        // Write completed frames to backend in sequential order
        while (nextFrameToWrite < numFrames &&
               outputFrames[nextFrameToWrite].has_value()) {
          if (!backend->writeFrame(*outputFrames[nextFrameToWrite])) {
            int32_t failedFrame = nextFrameToWrite.load();
            ORC_LOG_ERROR("ChromaSink: Failed to write frame {}", failedFrame);
            abortFlag.store(true);
            break;
          }
          // Free the frame memory immediately after writing
          outputFrames[nextFrameToWrite].reset();
          nextFrameToWrite++;
        }
      }

      // Update progress
      int32_t completed = completedFrames.fetch_add(1) + 1;
      if (progress_callback_ &&
          (completed % 10 == 0 || completed == numFrames)) {
        progress_callback_(completed, numFrames,
                           "Decoding frames: " + std::to_string(completed) +
                               "/" + std::to_string(numFrames));
      }
    }
  };

  // Create and start worker threads
  std::vector<std::thread> workers;
  workers.reserve(numThreads);
  for (int32_t i = 0; i < numThreads; i++) {
    workers.emplace_back(workerFunc);
  }

  // Wait for all workers to finish
  for (auto& worker : workers) {
    worker.join();
  }

  // Check if cancelled or error
  if (cancel_requested_.load() || abortFlag.load()) {
    ORC_LOG_WARN("ChromaSink: Decoding cancelled or failed");
    backend->finalize();  // Try to close cleanly
    trigger_status_ =
        cancel_requested_.load() ? "Cancelled by user" : "Error during decode";
    trigger_in_progress_.store(false);
    return false;
  }

  // Finalize output backend
  if (progress_callback_) {
    progress_callback_(numFrames, numFrames, "Finalizing output file...");
  }

  if (!backend->finalize()) {
    ORC_LOG_ERROR("ChromaSink: Failed to finalize output");
    trigger_status_ = "Error: Failed to finalize output file";
    trigger_in_progress_.store(false);
    return false;
  }

  // Calculate performance metrics
  auto decode_end_time = std::chrono::high_resolution_clock::now();
  auto decode_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      decode_end_time - decode_start_time);
  double decode_seconds = static_cast<double>(decode_duration.count()) / 1000.0;
  double fps = numFrames / decode_seconds;
  int32_t total_fields = numFrames * 2;
  [[maybe_unused]] double fields_per_second = total_fields / decode_seconds;

  ORC_LOG_INFO("ChromaSink: Successfully wrote {} frames to: {}", numFrames,
               output_path_);
  ORC_LOG_DEBUG(
      "ChromaSink: Performance - {:.2f} seconds, {:.2f} fps, {:.2f} fields/sec",
      decode_seconds, fps, fields_per_second);

  trigger_status_ = "Decode complete: " + std::to_string(numFrames) +
                    " frames (" +
                    std::to_string(static_cast<int>(fps * 10) / 10.0) + " fps)";
  trigger_in_progress_.store(false);

  if (progress_callback_) {
    progress_callback_(numFrames, numFrames, trigger_status_);
  }

  return true;
}

std::string ChromaSinkStage::get_trigger_status() const {
  return trigger_status_;
}

// Build a non-owning SourceField view into the VFrameR flat frame buffer.
// The VFrameR frame buffer layout is:
//   [field1_line0 | field1_line1 | … | field2_line0 | …]
// For PAL, lines 155 and 311 within each field carry 1136 samples (all other
// lines carry 1135 samples).  EBU Tech. 3280-E §1.3.1.
SourceField ChromaSinkStage::convertToSourceField(
    const orc::VideoFrameRepresentation* vfr, orc::FrameID frame_id,
    bool is_first_field, const orc::SourceParameters& videoParams) const {
  SourceField sf;

  const int16_t* frame_ptr = vfr->get_frame(frame_id);
  if (!frame_ptr) {
    ORC_LOG_WARN("ChromaSink: Frame {} has no data in VFrameR", frame_id);
    return sf;
  }

  sf.seq_no = static_cast<int32_t>(frame_id) + 1;
  sf.is_first_field = is_first_field;

  auto phase_hint = vfr->get_frame_phase_hint(frame_id);
  if (phase_hint.has_value()) {
    sf.frame_phase_id = static_cast<int32_t>(*phase_hint);
    ORC_LOG_TRACE("ChromaSink: Frame {} colour_frame_index={}", frame_id,
                  *phase_hint);
  }

  const bool is_pal = (videoParams.system == orc::VideoSystem::PAL ||
                       videoParams.system == orc::VideoSystem::PAL_M);

  // EBU Tech. 3280-E §1.3.1: frame-flat offset of the start of field 2.
  const size_t kPalField1Samples = frame_line_sample_offset(
      orc::VideoSystem::PAL,
      static_cast<size_t>(orc::kPalMaxSamplesPerLine - 1),
      static_cast<size_t>(orc::kPalField1Lines));

  // Helper: build PAL per-line pointer table for non-uniform line lengths.
  // frame_base_line: 0 for field 1, kPalField1Lines for field 2.
  auto buildPalLinePtrs =
      [](const int16_t* base, size_t line_count,
         size_t frame_base_line) -> std::vector<const int16_t*> {
    std::vector<const int16_t*> ptrs;
    ptrs.reserve(line_count);
    size_t offset = 0;
    for (size_t ln = 0; ln < line_count; ++ln) {
      ptrs.push_back(base + offset);
      offset += frame_line_sample_count(
          orc::VideoSystem::PAL,
          static_cast<size_t>(orc::kPalMaxSamplesPerLine - 1),
          frame_base_line + ln);
    }
    return ptrs;
  };

  if (is_pal) {
    const size_t field1_lines =
        static_cast<size_t>(orc::kPalField1Lines);  // 313
    const size_t field2_lines =
        static_cast<size_t>(orc::kPalFrameLines - orc::kPalField1Lines);  // 312

    sf.samples_per_line = 1135;  // nominal; lines 155/311 carry 1136

    if (is_first_field) {
      sf.data = frame_ptr;
      sf.line_count = field1_lines;
      sf.line_ptrs = buildPalLinePtrs(frame_ptr, field1_lines, 0);
    } else {
      sf.data = frame_ptr + kPalField1Samples;
      sf.line_count = field2_lines;
      sf.line_ptrs = buildPalLinePtrs(
          sf.data, field2_lines, static_cast<size_t>(orc::kPalField1Lines));
    }
  } else {
    // NTSC or PAL_M: uniform sampling, no line_ptrs needed
    const size_t spl = (videoParams.system == orc::VideoSystem::PAL_M)
                           ? static_cast<size_t>(orc::kPalMSamplesPerLine)
                           : static_cast<size_t>(orc::kNtscSamplesPerLine);
    const size_t field1_lines =
        static_cast<size_t>(orc::kNtscField1Lines);  // 263 (top spatial)
    const size_t field2_lines = static_cast<size_t>(
        orc::kNtscFrameLines - orc::kNtscField1Lines);  // 262 (bottom spatial)

    sf.samples_per_line = spl;
    if (is_first_field) {
      sf.data = frame_ptr;
      sf.line_count = field1_lines;
    } else {
      sf.data = frame_ptr + field1_lines * spl;
      sf.line_count = field2_lines;
    }
  }

  if (vfr->has_separate_channels()) {
    sf.is_yc = true;
    const int16_t* luma_ptr = vfr->get_frame_luma(frame_id);
    const int16_t* chroma_ptr = vfr->get_frame_chroma(frame_id);

    if (luma_ptr && chroma_ptr) {
      if (is_pal) {
        if (is_first_field) {
          sf.luma_data = luma_ptr;
          sf.chroma_data = chroma_ptr;
          sf.luma_line_ptrs = buildPalLinePtrs(luma_ptr, sf.line_count, 0);
          sf.chroma_line_ptrs = buildPalLinePtrs(chroma_ptr, sf.line_count, 0);
        } else {
          sf.luma_data = luma_ptr + kPalField1Samples;
          sf.chroma_data = chroma_ptr + kPalField1Samples;
          sf.luma_line_ptrs =
              buildPalLinePtrs(sf.luma_data, sf.line_count,
                               static_cast<size_t>(orc::kPalField1Lines));
          sf.chroma_line_ptrs =
              buildPalLinePtrs(sf.chroma_data, sf.line_count,
                               static_cast<size_t>(orc::kPalField1Lines));
        }
      } else {
        const size_t spl = sf.samples_per_line;
        const size_t field1_lines = static_cast<size_t>(orc::kNtscField1Lines);
        size_t field_offset = is_first_field ? 0 : field1_lines * spl;
        sf.luma_data = luma_ptr + field_offset;
        sf.chroma_data = chroma_ptr + field_offset;
      }
    }
  }

  ORC_LOG_TRACE(
      "ChromaSink: Frame {} {} field: line_count={} samples_per_line={} "
      "is_yc={} line_ptrs={}",
      frame_id, is_first_field ? "field1" : "field2", sf.line_count,
      sf.samples_per_line, sf.is_yc, sf.line_ptrs.size());

  return sf;
}

// Helper method: Write output frames to file
bool ChromaSinkStage::writeOutputFile(
    const std::string& output_path, const std::string& format,
    const std::vector<::ComponentFrame>& frames, const void* videoParamsPtr,
    const orc::VideoFrameRepresentation* vfr, uint64_t start_field_index,
    uint64_t num_fields, std::string& error_message) const {
  const auto& videoParams =
      *static_cast<const orc::SourceParameters*>(videoParamsPtr);
  if (frames.empty()) {
    ORC_LOG_ERROR("ChromaSink: No frames to write");
    error_message = "Error: No frames to write";
    return false;
  }

  // Create appropriate output backend
  auto backend = OutputBackendFactory::create(format);
  if (!backend) {
    ORC_LOG_ERROR("ChromaSink: Unknown or unsupported output format: {}",
                  format);
    ORC_LOG_ERROR(
        "ChromaSink: Available formats: rgb, yuv, y4m, mp4-h264, mp4-h265, "
        "mkv-ffv1");
    error_message = "Error: Unknown format '" + format +
                    "' - use rgb, yuv, y4m, or mp4-h264";
    return false;
  }

  // Configure backend
  OutputBackend::Configuration config;
  config.output_path = output_path;
  config.video_params = videoParams;
  config.padding_amount = output_padding_;
  config.options["format"] = format;

  // Pass encoder quality settings
  config.encoder_preset = encoder_preset_;
  config.encoder_crf = encoder_crf_;
  config.encoder_bitrate = encoder_bitrate_;

  // Pass audio information if embedding is enabled
  config.embed_audio = embed_audio_;
  config.embed_closed_captions = embed_closed_captions_;
  if (embed_audio_ && vfr && vfr->has_audio()) {
    config.vfr = vfr;
    config.start_field_index = start_field_index;
    config.num_fields = num_fields;
    ORC_LOG_DEBUG("ChromaSink: Audio embedding enabled for output");
  } else if (embed_audio_) {
    ORC_LOG_WARN(
        "ChromaSink: Audio embedding requested but no audio available");
  }

  // Initialize backend
  if (!backend->initialize(config)) {
    ORC_LOG_ERROR("ChromaSink: Failed to initialize {} output backend", format);
    ORC_LOG_ERROR("ChromaSink: Check log messages above for details");

    // Provide helpful error message based on format
    if (format.find("mp4-") == 0 || format.find("mkv-") == 0) {
      error_message =
          "Error: MP4/MKV encoder not installed - see logs. Use rgb/yuv/y4m "
          "instead.";
    } else {
      error_message =
          "Error: Failed to initialize " + format + " output - check logs";
    }
    return false;
  }

  ORC_LOG_DEBUG("ChromaSink: Writing {} frames as {}", frames.size(),
                backend->getFormatInfo());

  // Write all frames
  for (const auto& frame : frames) {
    if (!backend->writeFrame(frame)) {
      ORC_LOG_ERROR("ChromaSink: Failed to write frame");
      backend->finalize();  // Try to close cleanly
      error_message = "Error: Failed to write frame data - check logs";
      return false;
    }
  }

  // Finalize output
  if (!backend->finalize()) {
    ORC_LOG_ERROR("ChromaSink: Failed to finalize output");
    error_message = "Error: Failed to finalize output file - check logs";
    return false;
  }

  ORC_LOG_DEBUG("ChromaSink: Wrote {} frames to {}", frames.size(),
                output_path);
  return true;
}

std::vector<PreviewOption> ChromaSinkStage::get_preview_options() const {
  if (!cached_input_) {
    return {};
  }

  auto video_params = cached_input_->get_video_parameters();
  if (!video_params) {
    return {};
  }

  uint64_t frame_count = cached_input_->frame_count();
  if (frame_count < 1) {
    return {};  // Need at least 1 frame to decode
  }

  // Decode a test frame to get the actual full frame dimensions (with padding)
  uint32_t full_width = 0;
  uint32_t full_height = 0;

  if (frame_count > 0) {
    auto test_preview =
        render_preview("frame", 0, PreviewNavigationHint::Random);
    if (test_preview.width > 0 && test_preview.height > 0) {
      full_width = test_preview.width;
      full_height = test_preview.height;
    }
  }

  // Fallback to typical dimensions if decode failed
  if (full_width == 0 || full_height == 0) {
    full_width = 1135;  // Typical PAL with padding
    full_height = 625;
    if (video_params->system == VideoSystem::NTSC) {
      full_height = 505;  // Typical NTSC with padding
    }
  }

  // Get active picture area dimensions from metadata
  // These are used to calculate the DAR correction, not for the preview
  // dimensions
  uint32_t active_width = 702;   // Fallback PAL active picture width
  uint32_t active_height = 576;  // Fallback PAL active picture height

  if (video_params->active_video_start >= 0 &&
      video_params->active_video_end > video_params->active_video_start) {
    active_width =
        video_params->active_video_end - video_params->active_video_start;
  }
  if (video_params->first_active_frame_line >= 0 &&
      video_params->last_active_frame_line >
          video_params->first_active_frame_line) {
    active_height = video_params->last_active_frame_line -
                    video_params->first_active_frame_line;
  }

  // Calculate DAR correction based on active area for 4:3 display
  // The active picture area should display at 4:3 aspect ratio
  // Example: PAL 702x576 active → target ratio 4:3 = 1.333
  //          Current ratio: 702/576 = 1.219
  //          Need to multiply width by: 1.333/1.219 = 1.094 to reach proper 4:3
  double active_ratio =
      static_cast<double>(active_width) / static_cast<double>(active_height);
  double target_ratio = 4.0 / 3.0;
  double dar_correction = target_ratio / active_ratio;

  ORC_LOG_DEBUG(
      "ChromaSink: Preview dimensions: {}x{} (full frame), active area ~{}x{} "
      "(ratio={:.3f}), DAR correction = {:.3f} (target ratio=1.333)",
      full_width, full_height, active_width, active_height, active_ratio,
      dar_correction);

  // Only offer Frame mode for chroma decoder (fields are combined into RGB
  // frames)
  std::vector<PreviewOption> options;
  options.push_back(PreviewOption{"frame", "Frame (RGB)", false, full_width,
                                  full_height, frame_count, dar_correction});

  return options;
}

PreviewImage ChromaSinkStage::render_preview(const std::string& option_id,
                                             uint64_t index,
                                             PreviewNavigationHint hint
                                             [[maybe_unused]]) const {
  if (option_id != "frame") {
    return PreviewImage{};
  }

  auto carrier_opt = get_colour_preview_carrier(index, hint);
  if (!carrier_opt.has_value()) {
    return PreviewImage{};
  }

  auto result = render_preview_from_colour_carrier(*carrier_opt);
  result.vectorscope_data = carrier_opt->vectorscope_data;
  return result;
}

StagePreviewCapability ChromaSinkStage::get_preview_capability() const {
  StagePreviewCapability capability{};

  // Declare live-tweakable parameters independent of loaded preview data so
  // the GUI can build the tweak panel as soon as the node is selected.
  auto add_decode_tweaks = [&capability](std::optional<bool> is_pal_opt) {
    capability.tweakable_parameters.push_back(
        {"decoder_type", PreviewTweakClass::DecodePhase});
    capability.tweakable_parameters.push_back(
        {"chroma_gain", PreviewTweakClass::DecodePhase});
    capability.tweakable_parameters.push_back(
        {"chroma_phase", PreviewTweakClass::DecodePhase});
    capability.tweakable_parameters.push_back(
        {"luma_nr", PreviewTweakClass::DecodePhase});
    capability.tweakable_parameters.push_back(
        {"chroma_nr", PreviewTweakClass::DecodePhase});

    // If system is not known yet, include both sets; descriptor filtering
    // in the GUI removes keys not valid for the current project format.
    if (!is_pal_opt.has_value() || is_pal_opt.value()) {
      capability.tweakable_parameters.push_back(
          {"simple_pal", PreviewTweakClass::DecodePhase});
      capability.tweakable_parameters.push_back(
          {"transform_threshold", PreviewTweakClass::DecodePhase});
    }
    if (!is_pal_opt.has_value() || !is_pal_opt.value()) {
      capability.tweakable_parameters.push_back(
          {"ntsc_phase_comp", PreviewTweakClass::DecodePhase});
      capability.tweakable_parameters.push_back(
          {"chroma_weight", PreviewTweakClass::DecodePhase});
      capability.tweakable_parameters.push_back(
          {"adapt_threshold", PreviewTweakClass::DecodePhase});
    }
  };

  std::shared_ptr<const orc::VideoFrameRepresentation> local_input;
  {
    std::lock_guard<std::mutex> lock(cached_input_mutex_);
    local_input = cached_input_;
  }

  if (!local_input) {
    add_decode_tweaks(std::nullopt);
    return capability;
  }

  auto video_params_opt = local_input->get_video_parameters();
  if (!video_params_opt) {
    add_decode_tweaks(std::nullopt);
    return capability;
  }

  const auto& video_params = *video_params_opt;
  const bool is_pal = (video_params.system == VideoSystem::PAL ||
                       video_params.system == VideoSystem::PAL_M);

  capability.supported_data_types.push_back(is_pal ? VideoDataType::ColourPAL
                                                   : VideoDataType::ColourNTSC);
  capability.supported_data_types.push_back(
      local_input->has_separate_channels()
          ? (is_pal ? VideoDataType::YC_PAL : VideoDataType::YC_NTSC)
          : (is_pal ? VideoDataType::CompositePAL
                    : VideoDataType::CompositeNTSC));

  capability.navigation_extent.item_count = local_input->frame_count();
  capability.navigation_extent.granularity = 1;
  capability.navigation_extent.item_label = "frame";

  // Keep capability geometry aligned with legacy VFR preview sizing so
  // 4:3 mode remains consistent when switching between signal-domain and
  // colour-domain preview paths.
  capability.geometry.active_width = 702;
  capability.geometry.active_height = 576;
  if (video_params.active_video_start >= 0 &&
      video_params.active_video_end > video_params.active_video_start) {
    capability.geometry.active_width = static_cast<uint32_t>(
        video_params.active_video_end - video_params.active_video_start);
  }
  if (video_params.first_active_frame_line >= 0 &&
      video_params.last_active_frame_line >
          video_params.first_active_frame_line) {
    capability.geometry.active_height =
        static_cast<uint32_t>(video_params.last_active_frame_line -
                              video_params.first_active_frame_line);
  }

  if (capability.geometry.active_height == 0) {
    capability.geometry.active_height = is_pal ? 576u : 486u;
  }

  capability.geometry.display_aspect_ratio = 4.0 / 3.0;

  // Match DAR correction math used by PreviewHelpers/get_preview_options().
  double active_ratio = static_cast<double>(capability.geometry.active_width) /
                        static_cast<double>(capability.geometry.active_height);
  double target_ratio = 4.0 / 3.0;
  capability.geometry.dar_correction_factor = target_ratio / active_ratio;

  // Declare live-tweakable parameters (DecodePhase: affect the chroma decoder).
  // Output/file parameters (output_path, output_format, encoder_*) are
  // intentionally excluded from this list.
  add_decode_tweaks(is_pal);

  return capability;
}

std::optional<ColourFrameCarrier> ChromaSinkStage::get_colour_preview_carrier(
    uint64_t index, PreviewNavigationHint hint [[maybe_unused]]) const {
  ORC_LOG_DEBUG(
      "ChromaSink: get_colour_preview_carrier called on instance {} for frame "
      "{}, has_cached_input={}",
      static_cast<const void*>(this), index, (cached_input_ != nullptr));

  std::shared_ptr<const orc::VideoFrameRepresentation> local_input;
  {
    std::lock_guard<std::mutex> lock(cached_input_mutex_);
    local_input = cached_input_;
  }

  if (!local_input) {
    return std::nullopt;
  }

  auto video_params_opt = local_input->get_video_parameters();
  if (!video_params_opt) {
    return std::nullopt;
  }
  const SourceParameters& videoParams = *video_params_opt;
  SourceParameters safeVideoParams = videoParams;

  const auto preview_profile =
      decoder_video_profile_for_type(decoder_type_, safeVideoParams.system);
  if (!apply_decoder_safe_video_parameters(safeVideoParams, preview_profile,
                                           "ChromaSink preview")) {
    return std::nullopt;
  }

  // With VFrameR, frames are indexed directly (no field-parity offset needed).
  // Preview frame index == VFrameR frame_id offset into the frame range.
  const orc::FrameIDRange preview_frame_range = local_input->frame_range();
  const uint64_t frame_a_index = preview_frame_range.first + index;

  std::vector<SourceField> inputFields;
  int32_t num_lookbehind_frames = 0;
  int32_t num_lookahead_frames = 0;

  std::string temp_decoder_type = decoder_type_;
  if (temp_decoder_type == "transform3d") {
    // TransformPal3D::getLookBehind() = 1, getLookAhead() = 4 (frames).
    // Preview must supply at least getLookAhead()*2 = 8 fields after endIndex.
    num_lookbehind_frames = 2;
    num_lookahead_frames = 4;
  } else if (temp_decoder_type == "ntsc3d" ||
             temp_decoder_type == "ntsc3dnoadapt") {
    num_lookbehind_frames = 2;
    num_lookahead_frames = 2;
  } else {
    num_lookbehind_frames = 1;
    num_lookahead_frames = 1;
  }

  int64_t start_frame_idx =
      static_cast<int64_t>(frame_a_index) - num_lookbehind_frames;
  int64_t end_frame_idx =
      static_cast<int64_t>(frame_a_index) + num_lookahead_frames;

  const bool is_yc_source = local_input->has_separate_channels();
  const bool preview_is_pal =
      (safeVideoParams.system == orc::VideoSystem::PAL ||
       safeVideoParams.system == orc::VideoSystem::PAL_M);
  const int16_t preview_blanking =
      preview_is_pal ? static_cast<int16_t>(orc::kPalBlanking)
                     : static_cast<int16_t>(orc::kNtscBlanking);

  // Owned blank buffers for out-of-range frames; must outlive inputFields.
  std::deque<std::vector<int16_t>> previewBlankBuffers;

  // Helper: build a blank SourceField for preview
  auto makePreviewBlankField = [&](int64_t fi,
                                   bool is_first_field) -> SourceField {
    SourceField blank;
    blank.seq_no = static_cast<int32_t>(fi) + 1;
    blank.is_first_field = is_first_field;
    blank.is_yc = is_yc_source;

    // Compute samples for this field
    size_t samples;
    if (preview_is_pal) {
      const size_t pal_f1_samples = frame_line_sample_offset(
          orc::VideoSystem::PAL,
          static_cast<size_t>(orc::kPalMaxSamplesPerLine - 1),
          static_cast<size_t>(orc::kPalField1Lines));
      samples =
          is_first_field
              ? pal_f1_samples
              : (static_cast<size_t>(orc::kPalFrameSamples) - pal_f1_samples);
      blank.samples_per_line = 1135;
      blank.line_count =
          is_first_field
              ? static_cast<size_t>(orc::kPalField1Lines)
              : static_cast<size_t>(orc::kPalFrameLines - orc::kPalField1Lines);
    } else {
      size_t spl = (safeVideoParams.system == orc::VideoSystem::PAL_M)
                       ? static_cast<size_t>(orc::kPalMSamplesPerLine)
                       : static_cast<size_t>(orc::kNtscSamplesPerLine);
      size_t fl = is_first_field ? static_cast<size_t>(orc::kNtscField1Lines)
                                 : static_cast<size_t>(orc::kNtscFrameLines -
                                                       orc::kNtscField1Lines);
      samples = spl * fl;
      blank.samples_per_line = spl;
      blank.line_count = fl;
    }

    previewBlankBuffers.emplace_back(samples, preview_blanking);
    const int16_t* buf = previewBlankBuffers.back().data();

    blank.data = buf;
    if (is_yc_source) {
      blank.luma_data = buf;
      blank.chroma_data = buf;
    }

    if (preview_is_pal) {
      blank.line_ptrs.reserve(blank.line_count);
      const size_t preview_field_base =
          is_first_field ? 0 : static_cast<size_t>(orc::kPalField1Lines);
      size_t off = 0;
      for (size_t ln = 0; ln < blank.line_count; ++ln) {
        blank.line_ptrs.push_back(buf + off);
        off += frame_line_sample_count(
            orc::VideoSystem::PAL,
            static_cast<size_t>(orc::kPalMaxSamplesPerLine - 1),
            preview_field_base + ln);
      }
      if (is_yc_source) {
        blank.luma_line_ptrs = blank.line_ptrs;
        blank.chroma_line_ptrs = blank.line_ptrs;
      }
    }
    return blank;
  };

  for (int64_t fi = start_frame_idx; fi <= end_frame_idx; ++fi) {
    orc::FrameID fid = static_cast<orc::FrameID>(fi);
    if (fi >= 0 && local_input->has_frame(fid)) {
      auto sf1 =
          convertToSourceField(local_input.get(), fid, true, safeVideoParams);
      auto sf2 =
          convertToSourceField(local_input.get(), fid, false, safeVideoParams);
      if (sf1.data || sf1.luma_data) inputFields.push_back(std::move(sf1));
      if (sf2.data || sf2.luma_data) inputFields.push_back(std::move(sf2));
    } else {
      inputFields.push_back(makePreviewBlankField(fi, true));
      inputFields.push_back(makePreviewBlankField(fi, false));
    }
  }

  if (inputFields.size() < 2) {
    return std::nullopt;
  }

  // Determine the target frame's position within inputFields (field units).
  // Each frame contributes 2 SourceFields; lookbehind frames come first.
  const int32_t frameStartIndex = num_lookbehind_frames * 2;

  std::string effectiveDecoderType = decoder_type_;

  if (!preview_decoder_cache_.matches_config(
          effectiveDecoderType, chroma_gain_, chroma_phase_, luma_nr_,
          chroma_nr_, ntsc_phase_comp_, simple_pal_, false,
          transform_threshold_, chroma_weight_, adapt_threshold_)) {
    preview_decoder_cache_.mono_decoder.reset();
    preview_decoder_cache_.yc_mono_decoder.reset();
    preview_decoder_cache_.pal_decoder.reset();
    preview_decoder_cache_.ntsc_decoder.reset();
    preview_decoder_cache_.decoder_type = effectiveDecoderType;
    preview_decoder_cache_.chroma_gain = chroma_gain_;
    preview_decoder_cache_.chroma_phase = chroma_phase_;
    preview_decoder_cache_.luma_nr = luma_nr_;
    preview_decoder_cache_.chroma_nr = chroma_nr_;
    preview_decoder_cache_.ntsc_phase_comp = ntsc_phase_comp_;
    preview_decoder_cache_.simple_pal = simple_pal_;
    preview_decoder_cache_.blackandwhite = false;
    preview_decoder_cache_.transform_threshold = transform_threshold_;
    preview_decoder_cache_.chroma_weight = chroma_weight_;
    preview_decoder_cache_.adapt_threshold = adapt_threshold_;

    if (effectiveDecoderType == "mono") {
      MonoDecoder::MonoConfiguration config;
      config.yNRLevel = luma_nr_;
      config.filterChroma = false;
      config.videoParameters = safeVideoParams;
      preview_decoder_cache_.mono_decoder =
          std::make_unique<MonoDecoder>(config);
    } else if (effectiveDecoderType == "pal2d" ||
               effectiveDecoderType == "transform2d" ||
               effectiveDecoderType == "transform3d") {
      PalColour::Configuration config;
      config.chromaGain = chroma_gain_;
      config.chromaPhase = chroma_phase_;
      config.yNRLevel = luma_nr_;
      config.simplePAL = simple_pal_;
      config.transformThreshold = transform_threshold_;
      config.showFFTs = false;

      if (effectiveDecoderType == "transform3d") {
        config.chromaFilter = PalColour::transform3DFilter;
      } else if (effectiveDecoderType == "transform2d") {
        config.chromaFilter = PalColour::transform2DFilter;
      } else {
        config.chromaFilter = PalColour::palColourFilter;
      }

      preview_decoder_cache_.pal_decoder = std::make_unique<PalColour>();
      preview_decoder_cache_.pal_decoder->updateConfiguration(safeVideoParams,
                                                              config);
    } else {
      Comb::Configuration config;
      config.chromaGain = chroma_gain_;
      config.chromaPhase = chroma_phase_;
      config.cNRLevel = chroma_nr_;
      config.yNRLevel = luma_nr_;
      config.phaseCompensation = ntsc_phase_comp_;
      config.chromaWeight = chroma_weight_;
      config.adaptThreshold = adapt_threshold_;
      config.showMap = false;

      if (effectiveDecoderType == "ntsc1d") {
        config.dimensions = 1;
        config.adaptive = false;
      } else if (effectiveDecoderType == "ntsc3d") {
        config.dimensions = 3;
        config.adaptive = true;
      } else if (effectiveDecoderType == "ntsc3dnoadapt") {
        config.dimensions = 3;
        config.adaptive = false;
      } else {
        config.dimensions = 2;
        config.adaptive = false;
      }

      preview_decoder_cache_.ntsc_decoder = std::make_unique<Comb>();
      preview_decoder_cache_.ntsc_decoder->updateConfiguration(safeVideoParams,
                                                               config);
    }

    if (is_yc_source && effectiveDecoderType != "mono") {
      preview_decoder_cache_.yc_mono_decoder =
          create_yc_mono_decoder(safeVideoParams);
    }
  }

  std::vector<::ComponentFrame> outputFrames(1);
  const int32_t frameEndIndex = frameStartIndex + 2;

  if (preview_decoder_cache_.yc_mono_decoder) {
    std::vector<SourceField> ycYFields;
    std::vector<SourceField> ycCFields;
    ycYFields.reserve(inputFields.size());
    ycCFields.reserve(inputFields.size());

    for (const auto& field : inputFields) {
      SourceField yField = field;
      yField.is_yc = false;
      yField.data = field.luma_data;
      yField.luma_data = nullptr;
      yField.chroma_data = nullptr;
      yField.line_ptrs = field.luma_line_ptrs;
      yField.luma_line_ptrs.clear();
      yField.chroma_line_ptrs.clear();
      ycYFields.push_back(std::move(yField));

      SourceField cField = field;
      cField.is_yc = false;
      cField.data = field.chroma_data;
      cField.luma_data = nullptr;
      cField.chroma_data = nullptr;
      cField.line_ptrs = field.chroma_line_ptrs;
      cField.luma_line_ptrs.clear();
      cField.chroma_line_ptrs.clear();
      ycCFields.push_back(std::move(cField));
    }

    std::vector<::ComponentFrame> yFrames(1);
    std::vector<::ComponentFrame> uvFrames(1);

    preview_decoder_cache_.yc_mono_decoder->decodeFrames(
        ycYFields, frameStartIndex, frameEndIndex, yFrames);

    if (preview_decoder_cache_.pal_decoder) {
      preview_decoder_cache_.pal_decoder->decodeFrames(
          ycCFields, frameStartIndex, frameEndIndex, uvFrames);
    } else if (preview_decoder_cache_.ntsc_decoder) {
      preview_decoder_cache_.ntsc_decoder->decodeFrames(
          ycCFields, frameStartIndex, frameEndIndex, uvFrames);
    } else {
      uvFrames[0] = yFrames[0];
    }

    uvFrames[0].merge_luma_from(yFrames[0]);
    outputFrames[0] = std::move(uvFrames[0]);
  } else if (preview_decoder_cache_.mono_decoder) {
    preview_decoder_cache_.mono_decoder->decodeFrames(
        inputFields, frameStartIndex, frameEndIndex, outputFrames);
  } else if (preview_decoder_cache_.pal_decoder) {
    preview_decoder_cache_.pal_decoder->decodeFrames(
        inputFields, frameStartIndex, frameEndIndex, outputFrames);
  } else if (preview_decoder_cache_.ntsc_decoder) {
    preview_decoder_cache_.ntsc_decoder->decodeFrames(
        inputFields, frameStartIndex, frameEndIndex, outputFrames);
  }

  ::ComponentFrame& frame = outputFrames[0];
  int32_t width = frame.getWidth();
  int32_t height = frame.getHeight();
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }

  ColourFrameCarrier carrier{};
  carrier.data_type = (videoParams.system == VideoSystem::PAL ||
                       videoParams.system == VideoSystem::PAL_M)
                          ? VideoDataType::ColourPAL
                          : VideoDataType::ColourNTSC;
  carrier.colorimetry = (carrier.data_type == VideoDataType::ColourPAL)
                            ? ColorimetricMetadata::default_pal()
                            : ColorimetricMetadata::default_ntsc();
  carrier.system = videoParams.system;
  carrier.frame_index = index;
  carrier.width = static_cast<uint32_t>(width);
  carrier.height = static_cast<uint32_t>(height);
  carrier.active_x_start =
      (videoParams.active_video_start >= 0 &&
       videoParams.active_video_start < width)
          ? static_cast<uint32_t>(videoParams.active_video_start)
          : 0U;
  carrier.active_x_end =
      (videoParams.active_video_end > videoParams.active_video_start &&
       videoParams.active_video_end <= width)
          ? static_cast<uint32_t>(videoParams.active_video_end)
          : carrier.width;
  carrier.active_y_start =
      (videoParams.first_active_frame_line >= 0 &&
       videoParams.first_active_frame_line < height)
          ? static_cast<uint32_t>(videoParams.first_active_frame_line)
          : 0U;
  carrier.active_y_end =
      (videoParams.last_active_frame_line >
           videoParams.first_active_frame_line &&
       videoParams.last_active_frame_line <= height)
          ? static_cast<uint32_t>(videoParams.last_active_frame_line)
          : carrier.height;
  // CVBS_U10_4FSC normative levels from source parameters.
  carrier.cvbs_blanking = videoParams.blanking_level;
  carrier.cvbs_white = videoParams.white_level;

  carrier.vectorscope_data = VectorscopeAnalysisTool::extractFromComponentFrame(
      frame, videoParams, frame_a_index * 2, 4);
  if (carrier.vectorscope_data.has_value()) {
    carrier.vectorscope_data->system = videoParams.system;
    carrier.vectorscope_data->cvbs_white = videoParams.white_level;
    carrier.vectorscope_data->cvbs_blanking = videoParams.blanking_level;
  }

  const size_t samples =
      static_cast<size_t>(width) * static_cast<size_t>(height);
  carrier.y_plane.reserve(samples);
  carrier.u_plane.reserve(samples);
  carrier.v_plane.reserve(samples);

  for (int32_t y = 0; y < height; ++y) {
    const double* yLine = frame.y(y);
    const double* uLine = frame.u(y);
    const double* vLine = frame.v(y);
    for (int32_t x = 0; x < width; ++x) {
      carrier.y_plane.push_back(yLine[x]);
      carrier.u_plane.push_back(uLine[x]);
      carrier.v_plane.push_back(vLine[x]);
    }
  }

  if (!carrier.is_valid()) {
    return std::nullopt;
  }

  return carrier;
}

}  // namespace orc
