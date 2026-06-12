/*
 * File:        video_frame_representation.h
 * Module:      orc-core
 * Purpose:     VideoFrameRepresentation interface for CVBS_U10_4FSC frames
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <dropout_run.h>
#include <frame_descriptor.h>
#include <frame_id.h>
#include <orc_source_parameters.h>

#include "../hints/active_line_hint.h"
#include "video_metadata_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace orc {

// ============================================================================
// VideoFrameRepresentation
// ============================================================================
// Read-only access to a sequence of CVBS_U10_4FSC frames.
//
// Sample domain: int16_t in the CVBS_U10_4FSC 10-bit domain.
// Frames are stored in a flat contiguous buffer:
//   [field1_line0 | field1_line1 | … | field2_line0 | …]
// PAL frames have non-uniform line lengths; use the line-pointer table
// provided by dropout_util or the SourceField::line_ptrs mechanism when
// per-line access is required.
//
// Thread safety: all const methods may be called concurrently from multiple
// threads once the implementing object has been fully constructed.
class VideoFrameRepresentation {
 public:
  using sample_type = int16_t;  // CVBS_U10_4FSC 10-bit domain

  virtual ~VideoFrameRepresentation() = default;

  // --------------------------------------------------------------------------
  // Navigation
  // --------------------------------------------------------------------------

  // Half-open [first, last] range of valid FrameIDs.
  virtual FrameIDRange frame_range() const = 0;

  // Total number of frames available.
  virtual size_t frame_count() const = 0;

  // True if the given FrameID is present in this representation.
  virtual bool has_frame(FrameID id) const = 0;

  // Per-frame metadata descriptor. Returns nullopt when id is not present.
  virtual std::optional<FrameDescriptor> get_frame_descriptor(
      FrameID id) const = 0;

  // --------------------------------------------------------------------------
  // Flat sample access
  // --------------------------------------------------------------------------
  // Returned pointers are valid until the next call to any method on this
  // object or until the object is destroyed.  Do not retain pointers across
  // calls.

  // Pointer to the start of the flat frame buffer for the given frame.
  // Returns nullptr when id is not present.
  virtual const sample_type* get_frame(FrameID id) const = 0;

  // Pointer to line |line| (0-based) within the flat frame buffer.
  // For PAL, line lengths may be 1135 or 1136; callers must use the line
  // length from the SourceParameters or dropout_util to avoid overreading.
  // Returns nullptr when id is not present or line is out of range.
  virtual const sample_type* get_line(FrameID id, size_t line) const = 0;

  // Returns an owned copy of the complete frame buffer.
  virtual std::vector<sample_type> get_frame_copy(FrameID id) const = 0;

  // --------------------------------------------------------------------------
  // YC (separate luma / chroma) access
  // --------------------------------------------------------------------------
  // These methods are meaningful only for YC sources (has_separate_channels()
  // returns true).  For composite sources all YC methods return nullptr / {}.

  virtual bool has_separate_channels() const { return false; }

  virtual const sample_type* get_frame_luma(FrameID /*id*/) const {
    return nullptr;
  }
  virtual const sample_type* get_frame_chroma(FrameID /*id*/) const {
    return nullptr;
  }
  virtual const sample_type* get_line_luma(FrameID /*id*/,
                                           size_t /*line*/) const {
    return nullptr;
  }
  virtual const sample_type* get_line_chroma(FrameID /*id*/,
                                             size_t /*line*/) const {
    return nullptr;
  }

  // --------------------------------------------------------------------------
  // Hints
  // --------------------------------------------------------------------------

  // Dropout regions as frame-flat DropoutRun descriptors.
  virtual std::vector<DropoutRun> get_dropout_hints(FrameID /*id*/) const {
    return {};
  }

  // Colour-frame sequence index for the given frame (-1 when unknown).
  // Equivalent to FrameDescriptor::colour_frame_index; exposed as a separate
  // hint so transform stages can propagate phase without re-deriving it.
  virtual std::optional<int> get_frame_phase_hint(FrameID /*id*/) const {
    return std::nullopt;
  }

  // Active video line range (frame-flat, 0-based).
  virtual std::optional<ActiveLineHint> get_active_line_hint() const {
    return std::nullopt;
  }

  // Video parameters describing the signal geometry and level domain.
  virtual std::optional<SourceParameters> get_video_parameters() const {
    return std::nullopt;
  }

  // Raw VBI data for the given frame, if available.
  virtual std::optional<VbiData> get_vbi_hint(FrameID /*id*/) const {
    return std::nullopt;
  }

  // --------------------------------------------------------------------------
  // Audio
  // --------------------------------------------------------------------------

  virtual bool has_audio() const { return false; }

  // True when audio is sample-locked to video frames (frame-locked PCM).
  // False when audio is a free-running WAV stream independent of frame timing.
  virtual bool audio_locked() const { return false; }

  // Number of stereo int16_t pairs for the given frame.
  // Meaningful only when audio_locked() is true; returns 0 otherwise.
  virtual uint32_t get_audio_sample_count(FrameID /*id*/) const { return 0; }

  // Interleaved stereo int16_t pairs (L, R, L, R, …) for the given frame.
  virtual std::vector<int16_t> get_audio_samples(FrameID /*id*/) const {
    return {};
  }

  // --------------------------------------------------------------------------
  // EFM
  // --------------------------------------------------------------------------

  virtual bool has_efm() const { return false; }

  // Number of EFM t-values for the given frame (values in [3, 11]).
  virtual uint32_t get_efm_sample_count(FrameID /*id*/) const { return 0; }

  virtual std::vector<uint8_t> get_efm_samples(FrameID /*id*/) const {
    return {};
  }

  // --------------------------------------------------------------------------
  // AC3 RF
  // --------------------------------------------------------------------------

  virtual bool has_ac3_rf() const { return false; }

  // Number of QPSK dibit symbols for the given frame.
  virtual uint32_t get_ac3_symbol_count(FrameID /*id*/) const { return 0; }

  virtual std::vector<uint8_t> get_ac3_symbols(FrameID /*id*/) const {
    return {};
  }
};

// ============================================================================
// VideoFrameRepresentationWrapper
// ============================================================================
// Base for transform stages that wrap a source VFrameR and forward all methods
// unchanged except the ones they override.
//
// Implementing a transform stage:
//   1. Extend VideoFrameRepresentationWrapper.
//   2. Store the upstream VFrameR in source_ (set in the constructor).
//   3. Override only the methods that this stage modifies.
//
// Hint semantics: hints describe the OUTPUT of the stage, not its input.
// A stage that modifies data covered by a hint MUST override that hint method.
class VideoFrameRepresentationWrapper : public VideoFrameRepresentation {
 public:
  ~VideoFrameRepresentationWrapper() override = default;

  // Navigation
  FrameIDRange frame_range() const override {
    return source_ ? source_->frame_range() : FrameIDRange{};
  }
  size_t frame_count() const override {
    return source_ ? source_->frame_count() : 0;
  }
  bool has_frame(FrameID id) const override {
    return source_ ? source_->has_frame(id) : false;
  }
  std::optional<FrameDescriptor> get_frame_descriptor(
      FrameID id) const override {
    return source_ ? source_->get_frame_descriptor(id) : std::nullopt;
  }

  // Flat access
  const sample_type* get_frame(FrameID id) const override {
    return source_ ? source_->get_frame(id) : nullptr;
  }
  const sample_type* get_line(FrameID id, size_t line) const override {
    return source_ ? source_->get_line(id, line) : nullptr;
  }
  std::vector<sample_type> get_frame_copy(FrameID id) const override {
    return source_ ? source_->get_frame_copy(id) : std::vector<sample_type>{};
  }

  // YC
  bool has_separate_channels() const override {
    return source_ ? source_->has_separate_channels() : false;
  }
  const sample_type* get_frame_luma(FrameID id) const override {
    return source_ ? source_->get_frame_luma(id) : nullptr;
  }
  const sample_type* get_frame_chroma(FrameID id) const override {
    return source_ ? source_->get_frame_chroma(id) : nullptr;
  }
  const sample_type* get_line_luma(FrameID id, size_t line) const override {
    return source_ ? source_->get_line_luma(id, line) : nullptr;
  }
  const sample_type* get_line_chroma(FrameID id, size_t line) const override {
    return source_ ? source_->get_line_chroma(id, line) : nullptr;
  }

  // Hints
  std::vector<DropoutRun> get_dropout_hints(FrameID id) const override {
    return source_ ? source_->get_dropout_hints(id) : std::vector<DropoutRun>{};
  }
  std::optional<int> get_frame_phase_hint(FrameID id) const override {
    return source_ ? source_->get_frame_phase_hint(id) : std::nullopt;
  }
  std::optional<ActiveLineHint> get_active_line_hint() const override {
    return source_ ? source_->get_active_line_hint() : std::nullopt;
  }
  std::optional<SourceParameters> get_video_parameters() const override {
    return source_ ? source_->get_video_parameters() : std::nullopt;
  }
  std::optional<VbiData> get_vbi_hint(FrameID id) const override {
    return source_ ? source_->get_vbi_hint(id) : std::nullopt;
  }

  // Audio
  bool has_audio() const override {
    return source_ ? source_->has_audio() : false;
  }
  bool audio_locked() const override {
    return source_ ? source_->audio_locked() : false;
  }
  uint32_t get_audio_sample_count(FrameID id) const override {
    return source_ ? source_->get_audio_sample_count(id) : 0;
  }
  std::vector<int16_t> get_audio_samples(FrameID id) const override {
    return source_ ? source_->get_audio_samples(id) : std::vector<int16_t>{};
  }

  // EFM
  bool has_efm() const override {
    return source_ ? source_->has_efm() : false;
  }
  uint32_t get_efm_sample_count(FrameID id) const override {
    return source_ ? source_->get_efm_sample_count(id) : 0;
  }
  std::vector<uint8_t> get_efm_samples(FrameID id) const override {
    return source_ ? source_->get_efm_samples(id) : std::vector<uint8_t>{};
  }

  // AC3 RF
  bool has_ac3_rf() const override {
    return source_ ? source_->has_ac3_rf() : false;
  }
  uint32_t get_ac3_symbol_count(FrameID id) const override {
    return source_ ? source_->get_ac3_symbol_count(id) : 0;
  }
  std::vector<uint8_t> get_ac3_symbols(FrameID id) const override {
    return source_ ? source_->get_ac3_symbols(id) : std::vector<uint8_t>{};
  }

  std::shared_ptr<const VideoFrameRepresentation> get_source() const {
    return source_;
  }

 protected:
  explicit VideoFrameRepresentationWrapper(
      std::shared_ptr<const VideoFrameRepresentation> source)
      : source_(std::move(source)) {}

  std::shared_ptr<const VideoFrameRepresentation> source_;
};

using VideoFrameRepresentationPtr = std::shared_ptr<VideoFrameRepresentation>;

}  // namespace orc
