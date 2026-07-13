/*
 * File:        audio_align_stage.h
 * Module:      orc-stage-plugin-audio_align
 * Purpose:     Per-channel-pair audio sync adjustment transform stage
 *              (VFrameR) — shifts one audio channel pair in time relative
 *              to the video
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <orc/plugin/orc_stage_preview.h>
#include <orc/plugin/orc_stage_runtime.h>
#include <orc/stage/artifact.h>
#include <orc/stage/audio_channel_pair.h>
#include <orc/stage/stage_parameter.h>
#include <orc/stage/video_frame_representation.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orc {

// ============================================================================
// AlignedAudioChannelPairRepresentation
// ============================================================================
// Wraps a VideoFrameRepresentation and shifts one audio channel pair in time
// by a whole number of stereo pairs. Video, dropout hints, EFM/AC3 signal
// data and non-targeted channel pairs pass through untouched. A positive pair
// offset delays the audio relative to the video (inserts lead-in silence); a
// negative offset advances it (trims from the start).
//
// Per-frame pair counts are unchanged by the shift — each output frame |id|
// serves exactly audio_pairs_in_frame(id) pairs, assembled from the source
// frames whose windows contain the shifted positions and silence-filled past
// either end of the stream. Window boundaries are computed with cumulative
// audio_pair_offset() arithmetic, so the NTSC/PAL-M 1602/1601 cadence is
// handled exactly across frame boundaries.
//
// Thread safety: stateless after construction; safe for concurrent reads.
class AlignedAudioChannelPairRepresentation
    : public VideoFrameRepresentationWrapper,
      public Artifact {
 public:
  // |offset_pairs| is the offset in whole stereo pairs at the synchronous
  // 48000 Hz rate (positive = delay audio).
  AlignedAudioChannelPairRepresentation(
      std::shared_ptr<const VideoFrameRepresentation> source,
      size_t channel_pair, int64_t offset_pairs)
      : VideoFrameRepresentationWrapper(std::move(source)),
        Artifact(ArtifactID("audio_align"), Provenance{}),
        target_pair_(channel_pair),
        offset_pairs_(offset_pairs) {}

  std::string type_name() const override {
    return "aligned_audio_channel_pair_representation";
  }

  // --- Audio: only the target channel pair's samples change -----------------
  // (pair count and descriptors forward untouched; per-frame pair counts are
  // a model invariant and unchanged by the shift)

  std::vector<int32_t> get_audio_samples(size_t pair,
                                         FrameID id) const override;

 private:
  // Assembles the shifted window of the target channel pair for frame |id|
  // from the neighbouring frames' samples, silence-filling past either end
  // of the stream.
  std::vector<int32_t> assemble_shifted_window(FrameID id) const;

  size_t target_pair_;
  int64_t offset_pairs_;
};

// ============================================================================
// AudioAlignStage
// ============================================================================
class AudioAlignStage : public DAGStage,
                        public ParameterizedStage,
                        public IStagePreviewCapability {
 public:
  AudioAlignStage();

  std::string version() const override { return "1.0"; }
  ORC_STAGE_INSTRUCTIONS_MD
  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::TRANSFORM,
                        "audio_align",
                        "Audio Align",
                        "Shift one audio channel pair in time relative to the "
                        "video to correct audio/video sync",
                        1,
                        1,
                        1,
                        UINT32_MAX,
                        VideoFormatCompatibility::ALL,
                        SinkCategory::CORE,
                        "Transform"};
  }

  std::vector<ArtifactPtr> execute(
      const std::vector<ArtifactPtr>& inputs,
      const std::map<std::string, ParameterValue>& parameters,
      ObservationContext& observation_context) override;

  size_t required_input_count() const override { return 1; }
  size_t output_count() const override { return 1; }

  std::shared_ptr<const VideoFrameRepresentation> process(
      std::shared_ptr<const VideoFrameRepresentation> source) const;

  // ParameterizedStage
  std::vector<ParameterDescriptor> get_parameter_descriptors(
      VideoSystem project_format, SourceType source_type) const override;
  using ParameterizedStage::get_parameter_descriptors;
  std::map<std::string, ParameterValue> get_parameters() const override;
  bool set_parameters(
      const std::map<std::string, ParameterValue>& params) override;

  // IStagePreviewCapability
  StagePreviewCapability get_preview_capability() const override;

 private:
  int32_t channel_pair_ = 0;
  double offset_ms_ = 0.0;

  mutable std::shared_ptr<const VideoFrameRepresentation> cached_output_;
};

}  // namespace orc
