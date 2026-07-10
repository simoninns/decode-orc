/*
 * File:        stacker_stage.h
 * Module:      orc-core
 * Purpose:     Multi-source CVBS_U10_4FSC frame stacking stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <orc/plugin/orc_stage_preview.h>
#include <orc/plugin/orc_stage_runtime.h>
#include <orc/stage/dropout_run.h>
#include <orc/stage/frame_descriptor.h>
#include <orc/stage/frame_id.h>
#include <orc/stage/lru_cache.h>
#include <orc/stage/stage_parameter.h>
#include <orc/stage/video_frame_representation.h>

#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace orc {

// Forward declaration
class StackerStage;

// ============================================================================
// StackedVideoFrameRepresentation
// ============================================================================
// Wraps multiple source VFrameRs and stacks corresponding frames on demand.
//
// Frame alignment by colour_frame_index: when colour_frame_index is known
// (≥ 0) for all sources, frames are matched by colour_frame_index before
// stacking.  When it is -1 (unknown), temporal order is used as fallback.
//
// Padding frames (FrameDescriptor::is_padding_frame == true) are silently
// skipped and contribute no sample data to the stack.
//
// Thread safety: all const methods are safe for concurrent access; internal
// caches are protected by cache_mutex_.
class StackedVideoFrameRepresentation : public VideoFrameRepresentationWrapper,
                                        public Artifact {
 public:
  StackedVideoFrameRepresentation(
      const std::vector<std::shared_ptr<const VideoFrameRepresentation>>&
          sources,
      StackerStage* stage);

  ~StackedVideoFrameRepresentation() override = default;

  std::string type_name() const override {
    return "stacked_video_frame_representation";
  }

  // Navigation — report the range of the primary (first) source
  FrameIDRange frame_range() const override;
  size_t frame_count() const override;
  bool has_frame(FrameID id) const override;
  std::optional<FrameDescriptor> get_frame_descriptor(
      FrameID id) const override;

  // Flat access — stacks on demand, caches result
  const sample_type* get_frame(FrameID id) const override;
  const sample_type* get_line(FrameID id, size_t line) const override;
  std::vector<sample_type> get_frame_copy(FrameID id) const override;

  // YC
  bool has_separate_channels() const override;
  const sample_type* get_frame_luma(FrameID id) const override;
  const sample_type* get_frame_chroma(FrameID id) const override;
  const sample_type* get_line_luma(FrameID id, size_t line) const override;
  const sample_type* get_line_chroma(FrameID id, size_t line) const override;

  // Hints — after stacking only residual dropouts remain
  std::vector<DropoutRun> get_dropout_hints(FrameID id) const override;

  // Audio — track count and descriptors come from the first source carrying
  // audio; locked track 0 is stacked per the audio_stacking parameter (see
  // design §11.4)
  size_t audio_track_count() const override;
  std::optional<AudioTrackDescriptor> get_audio_track_descriptor(
      size_t track) const override;
  uint32_t get_audio_sample_count(size_t track, FrameID id) const override;
  std::vector<int16_t> get_audio_samples(size_t track,
                                         FrameID id) const override;

  // EFM
  bool has_efm() const override;
  uint32_t get_efm_sample_count(FrameID id) const override;
  std::vector<uint8_t> get_efm_samples(FrameID id) const override;

  // Resolve which source frame ID corresponds to the reference (first) frame
  // id when colour-frame-index alignment is active.
  FrameID resolve_source_frame(size_t src_idx, FrameID ref_id) const;

  // Count of sources that have a usable frame for ref_id
  size_t get_source_count(FrameID ref_id) const;

  friend class StackerStage;

 private:
  std::vector<std::shared_ptr<const VideoFrameRepresentation>> sources_;
  StackerStage* stage_;

  // LRU caches for stacked frames — composite and YC paths
  static constexpr size_t kMaxCachedFrames = 300;
  mutable LRUCache<FrameID, std::vector<sample_type>> stacked_frames_;
  mutable LRUCache<FrameID, std::vector<sample_type>> stacked_luma_;
  mutable LRUCache<FrameID, std::vector<sample_type>> stacked_chroma_;
  mutable LRUCache<FrameID, std::vector<DropoutRun>> stacked_dropouts_;
  mutable LRUCache<FrameID, std::vector<int16_t>> stacked_audio_;
  mutable LRUCache<FrameID, std::vector<uint8_t>> stacked_efm_;
  mutable LRUCache<FrameID, size_t> best_source_cache_;

  mutable std::mutex cache_mutex_;

  // Ensure the composite stacked frame is in cache (must hold cache_mutex_)
  void ensure_frame_stacked(FrameID id) const;

  // Ensure both YC stacked frames are in cache (must hold cache_mutex_)
  void ensure_frame_stacked_yc(FrameID id) const;

  // Find the source index with the fewest dropout samples for this frame
  size_t get_best_source_index(FrameID id) const;

  // Build the per-source frame ID vector for stacking (colour-frame aligned)
  std::vector<FrameID> collect_source_frame_ids(FrameID ref_id) const;
};

// ============================================================================
// StackerStage
// ============================================================================
class StackerStage : public DAGStage,
                     public ParameterizedStage,
                     public IStagePreviewCapability {
 public:
  StackerStage();

  std::string version() const override { return "1.0"; }
  ORC_STAGE_INSTRUCTIONS_MD
  NodeTypeInfo get_node_type_info() const override {
    return NodeTypeInfo{NodeType::MERGER,
                        "stacker",
                        "Stacker",
                        "Combine multiple VFrameR sources by stacking frames "
                        "for superior output quality (1 input = passthrough)",
                        1,
                        16,
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

  // IStagePreviewCapability
  StagePreviewCapability get_preview_capability() const override;

  std::shared_ptr<const VideoFrameRepresentation> process(
      const std::vector<std::shared_ptr<const VideoFrameRepresentation>>&
          sources) const;

  static size_t min_input_count() { return 1; }
  static size_t max_input_count() { return 16; }

  // ParameterizedStage
  std::vector<ParameterDescriptor> get_parameter_descriptors(
      VideoSystem project_format, SourceType source_type) const override;
  using ParameterizedStage::get_parameter_descriptors;
  std::map<std::string, ParameterValue> get_parameters() const override;
  bool set_parameters(
      const std::map<std::string, ParameterValue>& params) override;

  enum class AudioStackingMode { DISABLED, MEAN, MEDIAN };
  enum class EFMStackingMode { DISABLED, MEAN, MEDIAN };

  using sample_type = VideoFrameRepresentation::sample_type;

  friend class StackedVideoFrameRepresentation;

 private:
  int32_t m_mode = -1;
  int32_t m_smart_threshold = 15;
  bool m_no_diff_dod = false;
  bool m_passthrough = false;
  int32_t m_thread_count = 0;
  AudioStackingMode m_audio_stacking_mode = AudioStackingMode::MEAN;
  EFMStackingMode m_efm_stacking_mode = EFMStackingMode::MEAN;

  std::map<std::string, ParameterValue> parameters_;

  mutable std::mutex execute_mutex_;
  mutable std::shared_ptr<const VideoFrameRepresentation> cached_output_;
  mutable std::vector<std::shared_ptr<const VideoFrameRepresentation>>
      cached_sources_;

  // Stack composite frame from per-source FrameIDs
  void stack_frame(
      const std::vector<FrameID>& source_ids,
      const std::vector<std::shared_ptr<const VideoFrameRepresentation>>&
          sources,
      std::vector<sample_type>& output_samples,
      std::vector<DropoutRun>& output_dropouts) const;

  // Stack YC frame from per-source FrameIDs
  void stack_frame_yc(
      const std::vector<FrameID>& source_ids,
      const std::vector<std::shared_ptr<const VideoFrameRepresentation>>&
          sources,
      std::vector<sample_type>& output_luma,
      std::vector<sample_type>& output_chroma,
      std::vector<DropoutRun>& output_dropouts) const;

  // Apply chosen stacking mode to a set of sample values
  int16_t stack_mode(const std::vector<int16_t>& values,
                     bool all_dropout) const;

  int16_t compute_median(std::vector<int16_t> values) const;
  int32_t compute_mean(const std::vector<int16_t>& values) const;

  // Differential dropout detection: recover samples that are all marked as
  // dropout but are similar enough to be real signal.
  std::vector<int16_t> diff_dod(const std::vector<int16_t>& input,
                                int32_t black_level) const;

  // Line processing (parallel-friendly)
  void process_lines_range(
      size_t start_line, size_t end_line, size_t width, VideoSystem system,
      const std::vector<std::vector<sample_type>>& all_frames,
      const std::vector<bool>& frame_valid,
      const std::vector<std::vector<DropoutRun>>& all_dropouts,
      size_t num_sources, int32_t black_level, int32_t nominal_width,
      std::vector<sample_type>& output_samples,
      std::vector<DropoutRun>& output_dropouts, size_t& total_dropouts,
      size_t& total_stacked) const;

  void process_lines_range_yc(
      size_t start_line, size_t end_line, size_t width, VideoSystem system,
      const std::vector<std::vector<sample_type>>& all_luma,
      const std::vector<std::vector<sample_type>>& all_chroma,
      const std::vector<bool>& frame_valid,
      const std::vector<std::vector<DropoutRun>>& all_dropouts,
      size_t num_sources, int32_t black_level, int32_t nominal_width,
      std::vector<sample_type>& output_luma,
      std::vector<sample_type>& output_chroma,
      std::vector<DropoutRun>& output_dropouts, size_t& total_dropouts,
      size_t& total_stacked) const;

  // Audio and EFM stacking helpers
  std::vector<int16_t> stack_audio(
      const std::vector<FrameID>& source_ids,
      const std::vector<std::shared_ptr<const VideoFrameRepresentation>>&
          sources,
      size_t best_src) const;

  std::vector<uint8_t> stack_efm(
      const std::vector<FrameID>& source_ids,
      const std::vector<std::shared_ptr<const VideoFrameRepresentation>>&
          sources,
      size_t best_src) const;

  int16_t audio_mean(const std::vector<int16_t>& v) const;
  int16_t audio_median(std::vector<int16_t> v) const;
  uint8_t efm_mean(const std::vector<uint8_t>& v) const;
  uint8_t efm_median(std::vector<uint8_t> v) const;
};

}  // namespace orc
