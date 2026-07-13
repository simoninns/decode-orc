/*
 * File:        audio_channel_map_stage.h
 * Module:      orc-stage-plugin-audio_channel_map
 * Purpose:     Audio channel routing transform stage (VFrameR) — delete,
 *              SMPTE 272M mono extraction, and channel swap for a target
 *              audio channel pair
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

#include <memory>
#include <string>
#include <vector>

namespace orc {

// One channel-routing operation. The source channel pair is the one the
// operation reads from; the routing operations additionally write to a target
// channel pair (a new appended pair, or an existing one).
//
// The mono operations are SMPTE 272M-1994 conformant: a mono programme is
// carried on the left channel only, with the right channel silenced (all
// zeros) — never duplicated across both channels.
enum class AudioChannelMapOperation {
  kDelete,             // remove the source channel pair; later pairs shift down
  kLeftToMono,         // in place: left channel kept, right channel zeroed
  kRightToMono,        // in place: right channel moved to left, right zeroed
  kCopyLeftToTarget,   // write [L, 0] from the source to the target pair
  kCopyRightToTarget,  // write [R, 0] from the source to the target pair
};

// ============================================================================
// ChannelMappedRepresentation
// ============================================================================
// Wraps a VideoFrameRepresentation and applies one channel-routing operation.
// Video, dropout hints, EFM/AC3 signal data and untouched channel pairs pass
// through unchanged.
//
// - delete removes the source pair; later pairs shift down one index (count
// −1).
// - left_to_mono / right_to_mono overwrite the source pair in place with
//   SMPTE 272M mono (chosen channel on the left, right zeroed); count
//   unchanged.
// - copy_left_to_target / copy_right_to_target read the source pair and write
//   the mono channel to the target pair, leaving the source untouched. When the
//   target is "new" the mono pair is appended (count +1); when it is an
//   existing pair that pair is overwritten (count unchanged).
//
// The produced mono pair reports origin DERIVED; its name is |description| when
// |override_description| is set, otherwise the source pair's existing name is
// kept unchanged. The remap is a pure per-sample channel operation on the
// synchronous 24-bit-in-int32 stereo pairs; timing and per-frame pair counts
// are untouched.
//
// Thread safety: stateless after construction; safe for concurrent reads.
class ChannelMappedRepresentation : public VideoFrameRepresentationWrapper,
                                    public Artifact {
 public:
  ChannelMappedRepresentation(
      std::shared_ptr<const VideoFrameRepresentation> source,
      size_t source_pair, AudioChannelMapOperation operation,
      bool target_is_new, size_t target_pair, bool override_description,
      std::string description)
      : VideoFrameRepresentationWrapper(std::move(source)),
        Artifact(ArtifactID("audio_channel_map"), Provenance{}),
        source_pair_(source_pair),
        operation_(operation),
        target_is_new_(target_is_new),
        target_pair_(target_pair),
        override_description_(override_description),
        description_(std::move(description)) {}

  std::string type_name() const override {
    return "channel_mapped_representation";
  }

  // --- Audio channel pairs --------------------------------------------------

  size_t audio_channel_pair_count() const override;
  std::optional<AudioChannelPairDescriptor> get_audio_channel_pair_descriptor(
      size_t pair) const override;
  std::vector<int32_t> get_audio_samples(size_t pair,
                                         FrameID id) const override;

 private:
  // How the channels of an output pair derive from its source pair.
  enum class ChannelFill {
    kNone,      // pass through unchanged
    kLeftMono,  // SMPTE 272M mono: keep L, zero R
    kRightMono  // SMPTE 272M mono: move R to L, zero R
  };

  size_t source_pair_count() const {
    return source_ ? source_->audio_channel_pair_count() : 0;
  }
  bool is_delete() const {
    return operation_ == AudioChannelMapOperation::kDelete;
  }
  bool is_route() const {
    return operation_ == AudioChannelMapOperation::kCopyLeftToTarget ||
           operation_ == AudioChannelMapOperation::kCopyRightToTarget;
  }
  bool is_append() const { return is_route() && target_is_new_; }
  // The mono fill implied by a left/right operation.
  ChannelFill mono_fill() const {
    return (operation_ == AudioChannelMapOperation::kLeftToMono ||
            operation_ == AudioChannelMapOperation::kCopyLeftToTarget)
               ? ChannelFill::kLeftMono
               : ChannelFill::kRightMono;
  }
  // Whether output pair |pair| is the mono pair this operation produces.
  bool is_result_pair(size_t pair) const;

  // Source pair index serving output pair |pair|.
  size_t source_pair_for(size_t pair) const;
  ChannelFill fill_for_pair(size_t pair) const;

  // Applies |fill| in place to interleaved stereo samples.
  static void apply_fill(std::vector<int32_t>& samples, ChannelFill fill);

  size_t source_pair_;
  AudioChannelMapOperation operation_;
  bool target_is_new_;
  size_t target_pair_;
  bool
      override_description_;  // true: use description_; false: keep source name
  std::string description_;   // custom name for the result pair
};

// ============================================================================
// AudioChannelMapStage
// ============================================================================
class AudioChannelMapStage : public DAGStage,
                             public ParameterizedStage,
                             public IStagePreviewCapability {
 public:
  AudioChannelMapStage();

  std::string version() const override { return "1.0"; }
  ORC_STAGE_INSTRUCTIONS_MD
  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::TRANSFORM,
                        "audio_channel_map",
                        "Audio Channel Map",
                        "Route audio channel pairs: delete a pair, extract a "
                        "left/right channel as SMPTE 272M mono in place, or "
                        "copy it as mono to a new or existing target pair",
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
  int32_t channel_pair_ = 0;  // source channel pair
  std::string operation_ = "left_to_mono";
  // Target for the routing operations: "new" appends a pair, otherwise the
  // string is the destination pair index. Ignored by the other operations.
  std::string target_pair_ = "new";
  // When true, description_ overrides the result pair's name; when false the
  // pair keeps its existing description. Gated in the GUI by an "Add
  // description" checkbox.
  bool set_description_ = false;
  // Custom name for the result pair (target for copy ops, source for the
  // in-place mono ops), used only when set_description_ is true.
  std::string description_;

  mutable std::shared_ptr<const VideoFrameRepresentation> cached_output_;
};

}  // namespace orc
