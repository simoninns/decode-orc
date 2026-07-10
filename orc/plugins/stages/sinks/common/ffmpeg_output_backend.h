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

#include <orc/stage/audio_track.h>
#include <orc/stage/eia608_decoder.h>
#include <orc/stage/field_id.h>
#include <orc/stage/frame_id.h>

#include <memory>
#include <string>
#include <vector>

#include "output_backend.h"

#ifdef HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
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
  bool writeFrame(const ::ComponentFrame& frame) override;
  bool finalize() override;
  std::string getFormatInfo() const override;

 private:
  // FFmpeg context structures
  AVFormatContext* format_ctx_ = nullptr;
  AVCodecContext* codec_ctx_ = nullptr;
  AVStream* stream_ = nullptr;
  AVFrame* frame_ = nullptr;  // Destination frame (encoder's pixel format)
  AVFrame* src_frame_ =
      nullptr;  // Source frame (YUV444P16LE from ComponentFrame)
  AVPacket* packet_ = nullptr;
  SwsContext* sws_ctx_ = nullptr;

  // Video filter graph (bwdif deinterlacing and/or user-supplied -vf chain).
  // When active, the graph replaces the swscale conversion path: frames enter
  // as YUV444P16LE and leave in the encoder's pixel format (libavfilter
  // auto-inserts the conversion). buffersrc/buffersink are owned by the graph.
  AVFilterGraph* filter_graph_ = nullptr;
  AVFilterContext* buffersrc_ctx_ = nullptr;
  AVFilterContext* buffersink_ctx_ = nullptr;
  AVFrame* filtered_frame_ = nullptr;  // Reused for buffersink output
  std::string video_filter_desc_;      // Combined chain; empty = passthrough

  // Audio structures — one encoder per embedded pipeline track.
  struct AudioTrackEncoder {
    size_t track_index = 0;           // Pipeline track index
    AudioTrackDescriptor descriptor;  // Name, origin, lock mode, exact rate
    int sample_rate = 44100;          // Integer rate declared to the encoder
    AVCodecContext* codec_ctx = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    int64_t pts = 0;
    std::vector<int16_t> buffer;  // Pending interleaved S16 samples

    // Free-running feed state (descriptor.locked == false). The stream is
    // consumed by its own pair cursor rather than the video frame cursor;
    // per-frame windows come from audio_stream_pair_offset().
    uint64_t next_pair = 0;    // Absolute stream pair cursor
    uint64_t total_pairs = 0;  // Stream length in pairs
    uint64_t frames_fed = 0;   // Video frames fed so far
  };
  std::vector<AudioTrackEncoder> audio_encoders_;
  AVPacket* audio_packet_ = nullptr;  // Shared scratch packet
  const VideoFrameRepresentation* vfr_ = nullptr;
  uint64_t start_field_index_ = 0;
  uint64_t num_fields_ = 0;
  uint64_t current_field_for_audio_ = 0;
  uint64_t rep_first_frame_ = 0;  // First FrameID of the representation
                                  // (free-running stream pair 0 origin)
  bool embed_audio_ = false;
  std::string audio_tracks_option_;  // "all" or comma-separated indices
  double audio_gain_ = 1.0;  // Linear gain applied to embedded audio samples

  // Subtitle structures (for closed captions)
  AVCodecContext* subtitle_codec_ctx_ = nullptr;
  AVStream* subtitle_stream_ = nullptr;
  bool embed_closed_captions_ = false;
  uint64_t current_field_for_captions_ = 0;
  std::unique_ptr<EIA608Decoder> eia608_decoder_;
  std::vector<CaptionCue> pending_cues_;
  size_t next_cue_index_ = 0;

  // Chapter metadata
  bool embed_chapter_metadata_ = false;

  // State
  int64_t pts_ = 0;
  int frames_written_ = 0;
  std::string codec_name_;
  std::string container_format_;

  // Video parameters
  int width_ = 0;  // Encoder input dimensions (active area padded to even)
  int height_ = 0;
  int src_width_ = 0;  // Source ComponentFrame dimensions (before padding)
  int src_height_ = 0;
  int active_width_ = 0;  // Active video region dimensions
  int active_height_ = 0;
  AVRational time_base_;               // Source (pre-filter) frame timing
  AVRational enc_time_base_ = {0, 1};  // Encoder/stream time base; differs
                                       // from time_base_ when a filter chain
                                       // changes the frame rate
  AVRational requested_dar_ = {0, 1};  // Display aspect ratio override
                                       // ({0,1} = auto/unspecified)
  AVRational sample_aspect_ratio_ = {0, 1};  // Effective SAR stamped on
                                             // output ({0,1} = unspecified)
  VideoSystem video_system_ = VideoSystem::PAL;
  double black_ire_ = 0.0;
  double white_ire_ = 0.0;
  orc::SourceParameters video_params_;

  // Crop parameters
  int crop_top_ = 0;

  // Encoder quality settings
  std::string encoder_preset_ = "medium";
  int encoder_crf_ = 18;
  int encoder_bitrate_ = 0;
  bool use_lossless_mode_ = false;
  std::string prores_profile_ = "hq";
  bool is_tff_ = false;  // True when the padded output frame should be marked
                         // top-field-first.

  // Helper methods
  bool setupEncoder(const std::string& codec_id,
                    const orc::SourceParameters& params);
  // Build and configure the libavfilter graph from video_filter_desc_.
  // Requires width_/height_/time_base_ and codec_ctx_->pix_fmt to be set.
  bool setupFilterGraph();
  void freeFilterGraph();
  // Pull all currently available frames from the buffersink and encode them.
  bool drainFilterGraph();
  // Send one frame to the encoder and write the resulting packets.
  // frame may be nullptr to flush the encoder.
  bool encodeVideoFrame(AVFrame* frame);
  // Set up one encoder per selected audio track (audio_tracks_option_).
  bool setupAudioEncoders();
  bool setupAudioEncoderForTrack(AudioTrackEncoder& track);
  // Gather one video frame's worth of samples for the given track (locked:
  // per-frame accessor; free-running: stream window for the frame).
  std::vector<int16_t> gatherAudioForFrame(AudioTrackEncoder& track,
                                           FrameID frame_id);
  // Encode full encoder-frame-size chunks from the track's pending buffer.
  bool encodeBufferedAudio(AudioTrackEncoder& track);
  // Send one audio frame (nullptr flushes) and write the resulting packets.
  bool sendAudioFrame(AudioTrackEncoder& track, AVFrame* frame);
  bool setupSubtitleEncoder();
  void extractClosedCaptionsFromObservations(
      const class IObservationContext& observation_context,
      uint64_t field_start, uint64_t field_count);
  void setupChapterMetadata(
      const class IObservationContext& observation_context);
  bool encodeAudioForFrame();
  bool encodeClosedCaptionsForFrame();
  bool convertAndEncode(const ComponentFrame& component_frame);
  void cleanup();
};

}  // namespace orc

#endif  // HAVE_FFMPEG

#endif  // ORC_CORE_FFMPEG_OUTPUT_BACKEND_H
