/*
 * File:        video_field_representation.h
 * Module:      orc-core
 * Purpose:     Video field representation interface
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#pragma once

#include "field_id.h"
#include "artifact.h"
#include "dropout_decision.h"
#include "tbc_metadata.h"
#include "../hints/field_parity_hint.h"
#include "../hints/pal_phase_hint.h"
#include "../hints/active_line_hint.h"
// Note: pal_phase_hint.h contains FieldPhaseHint (works for both PAL and NTSC)
#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>
#include <memory>

namespace orc {

// Forward declarations
class Observation;

/**
 * @brief Field parity (interlacing information)
 */
enum class FieldParity {
    Top,     // Top field (odd lines in progressive numbering)
    Bottom   // Bottom field (even lines)
};

/**
 * @brief Video standard/format
 */
enum class VideoFormat {
    NTSC,
    PAL,
    Unknown
};

/**
 * @brief Descriptor for a single video field
 */
struct FieldDescriptor {
    FieldID field_id;
    FieldParity parity;
    VideoFormat format;
    size_t width;          // Samples per line
    size_t height;         // Number of lines
    
    // Optional: timing information from VBI if available
    std::optional<int32_t> frame_number;
    std::optional<uint32_t> timecode;
};

/**
 * @brief Abstract interface for accessing video field samples
 * 
 * A Video Field Representation provides read-only access to field samples.
 * Concrete implementations may be:
 * - Raw TBC fields
 * - Dropout-corrected fields
 * - Stacked or filtered fields
 * 
 * All sample data is immutable from the client perspective.
 */
class VideoFieldRepresentation : public Artifact {
public:
    using sample_type = uint16_t;  // 16-bit samples (standard for TBC data)
    
    virtual ~VideoFieldRepresentation() = default;
    
    // Sequence information
    virtual FieldIDRange field_range() const = 0;
    virtual size_t field_count() const = 0;
    virtual bool has_field(FieldID id) const = 0;
    
    // Field metadata
    virtual std::optional<FieldDescriptor> get_descriptor(FieldID id) const = 0;
    
    // Sample access (read-only)
    // Returns pointer to line data, or nullptr if field/line not available
    // Lifetime: pointer valid until next call to get_line or object destruction
    virtual const sample_type* get_line(FieldID id, size_t line) const = 0;
    
    // Bulk access (returns copy)
    virtual std::vector<sample_type> get_field(FieldID id) const = 0;
    
    // ========================================================================
    // DUAL-CHANNEL ACCESS - For YC sources (separate Y and C files)
    // ========================================================================
    // YC sources provide luma (Y) and chroma (C) in separate files, as opposed
    // to composite sources where Y+C are modulated together. This allows cleaner
    // luma (no comb filter artifacts) and simpler chroma decoding.
    //
    // For composite sources, these methods return false/nullptr/{}.
    // For YC sources, has_separate_channels() returns true and the line/field
    // methods provide access to Y and C independently.
    
    /**
     * @brief Check if this representation has separate Y and C channels
     * 
     * @return True for YC sources, false for composite sources
     */
    virtual bool has_separate_channels() const {
        return false;  // Default: composite (Y+C modulated together)
    }
    
    /**
     * @brief Get luma (Y) line data for YC sources
     * 
     * @param id Field ID
     * @param line Line number
     * @return Pointer to Y samples, or nullptr if not available or composite source
     */
    virtual const sample_type* get_line_luma(FieldID /*id*/, size_t /*line*/) const {
        return nullptr;  // Default: not a YC source
    }
    
    /**
     * @brief Get chroma (C) line data for YC sources
     * 
     * @param id Field ID
     * @param line Line number
     * @return Pointer to C samples, or nullptr if not available or composite source
     */
    virtual const sample_type* get_line_chroma(FieldID /*id*/, size_t /*line*/) const {
        return nullptr;  // Default: not a YC source
    }
    
    /**
     * @brief Get luma (Y) field data for YC sources
     * 
     * @param id Field ID
     * @return Vector of Y samples (empty if not available or composite source)
     */
    virtual std::vector<sample_type> get_field_luma(FieldID /*id*/) const {
        return {};  // Default: not a YC source
    }
    
    /**
     * @brief Get chroma (C) field data for YC sources
     * 
     * @param id Field ID
     * @return Vector of C samples (empty if not available or composite source)
     */
    virtual std::vector<sample_type> get_field_chroma(FieldID /*id*/) const {
        return {};  // Default: not a YC source
    }
    
    // ========================================================================
    // HINTS - Information from upstream processors (e.g., ld-decode)
    // ========================================================================
    // Hints are metadata provided by external tools that analyzed the video.
    // They should be preferred over observations when available, as they
    // represent the original processor's determination.
    
    // Dropout hints (from TBC decoder like ld-decode)
    // Returns empty vector if source has no dropout information
    virtual std::vector<DropoutRegion> get_dropout_hints(FieldID /*id*/) const {
        return {};  // Default: no hints
    }
    
    // Field parity hint (from TBC metadata like ld-decode's is_first_field)
    // Returns empty optional if source has no field parity information
    virtual std::optional<FieldParityHint> get_field_parity_hint(FieldID /*id*/) const {
        return std::nullopt;  // Default: no hint
    }
    
    // Field phase hint (from TBC metadata like ld-decode's field_phase_id)
    // Works for both PAL (8-phase) and NTSC (4-phase)
    // Returns empty optional if source has no phase information
    virtual std::optional<FieldPhaseHint> get_field_phase_hint(FieldID /*id*/) const {
        return std::nullopt;  // Default: no hint
    }
    
    // Active line range hint (from TBC metadata like ld-decode's active line ranges)
    // Provides the vertical region containing visible video content
    // Returns empty optional if source has no active line information
    virtual std::optional<ActiveLineHint> get_active_line_hint() const {
        return std::nullopt;  // Default: no hint
    }
    
    // ========================================================================
    // METADATA - Video parameters and configuration
    // ========================================================================
    
    // Video parameters (metadata from source, e.g., TBC metadata)
    // Returns empty optional if source has no video parameter information
    // Stages should propagate this through the DAG chain
    virtual std::optional<VideoParameters> get_video_parameters() const {
        return std::nullopt;  // Default: no parameters
    }
    
    // ========================================================================
    // OBSERVATIONS - Analysis results from orc-core stages
    // ========================================================================
    // Observations are computed by orc-core's own analysis (observers).
    // They should only be used when hints are not available.
    
    // Observation access (metadata from source or computed by stages)
    // Returns observations for a specific field (e.g., field parity, VBI data)
    // This allows observation history to flow through the DAG, enabling
    // stages that merge multiple sources to provide complete history
    virtual std::vector<std::shared_ptr<Observation>> get_observations(FieldID /*id*/) const {
        return {};  // Default: no observations
    }
    
    // ========================================================================
    // AUDIO - PCM audio data access
    // ========================================================================
    
    /**
     * @brief Get number of audio samples for a specific field
     * 
     * Returns the number of stereo PCM audio samples (44.1kHz, 16-bit signed)
     * that correspond to this field. Returns 0 if no audio is available.
     * 
     * @param id Field ID
     * @return Number of audio samples (0 if no audio)
     */
    virtual uint32_t get_audio_sample_count(FieldID /*id*/) const {
        return 0;  // Default: no audio
    }
    
    /**
     * @brief Get audio samples for a specific field
     * 
     * Returns interleaved stereo PCM audio samples (L, R, L, R, ...)
     * Format: 16-bit signed integer, little endian, 44.1kHz stereo
     * 
     * @param id Field ID
     * @return Vector of audio samples (empty if no audio)
     */
    virtual std::vector<int16_t> get_audio_samples(FieldID /*id*/) const {
        return {};  // Default: no audio
    }
    
    /**
     * @brief Check if audio data is available
     * 
     * @return True if this representation has audio data
     */
    virtual bool has_audio() const {
        return false;  // Default: no audio
    }
    
    // ========================================================================
    // EFM - EFM (Eight to Fourteen Modulation) data access
    // ========================================================================
    
    /**
     * @brief Get number of EFM t-values for a specific field
     * 
     * Returns the number of EFM t-values that correspond to this field.
     * T-values are 8-bit values from 3 to 11 (inclusive).
     * Returns 0 if no EFM data is available.
     * 
     * @param id Field ID
     * @return Number of EFM t-values (0 if no EFM)
     */
    virtual uint32_t get_efm_sample_count(FieldID /*id*/) const {
        return 0;  // Default: no EFM
    }
    
    /**
     * @brief Get EFM t-values for a specific field
     * 
     * Returns EFM t-values as 8-bit unsigned integers.
     * Valid t-values are in the range [3, 11] inclusive.
     * Values outside this range are invalid.
     * 
     * @param id Field ID
     * @return Vector of EFM t-values (empty if no EFM)
     */
    virtual std::vector<uint8_t> get_efm_samples(FieldID /*id*/) const {
        return {};  // Default: no EFM
    }
    
    /**
     * @brief Check if EFM data is available
     * 
     * @return True if this representation has EFM data
     */
    virtual bool has_efm() const {
        return false;  // Default: no EFM
    }
    
    // Type information
    std::string type_name() const override { return "VideoFieldRepresentation"; }
    
protected:
    VideoFieldRepresentation(ArtifactID id, Provenance prov)
        : Artifact(std::move(id), std::move(prov)) {}
};

/**
 * @brief Base class for VideoFieldRepresentation wrappers
 * 
 * This class automatically propagates all hints and metadata from the source
 * through the DAG chain, eliminating code duplication in wrapper implementations.
 * 
 * Wrapper implementations only need to override methods they actually modify
 * (typically get_line() and/or get_field()).
 * 
 * IMPORTANT: Hint Semantics
 * -------------------------
 * Hints describe the OUTPUT of each stage, not the input. This means:
 * 
 * - If a stage modifies data that hints describe, it MUST override the hint methods
 *   to reflect the modified state. For example:
 *   * Dropout correction stage should return EMPTY dropout hints (all corrected)
 *   * Field reordering stage should update field descriptors with new ordering
 *   * Chroma decoding stage might add/modify format information
 *   * Crop stage should update video parameters with new active area
 *   * Scale stage should update video parameters with new dimensions
 * 
 * - If a stage does NOT modify the hinted data, it inherits the default behavior
 *   which forwards hints unchanged. For example:
 *   * Brightness adjustment preserves all hints
 *   * Color correction preserves dropout hints and geometry
 * 
 * Video parameters (active_video_start/end, field dimensions, etc.) are hints too
 * and follow the same semantic - they describe the output video geometry.
 * 
 * This ensures each stage in the chain receives accurate information about its input.
 */
class VideoFieldRepresentationWrapper : public VideoFieldRepresentation {
public:
    virtual ~VideoFieldRepresentationWrapper() = default;
    
    // Automatically forward sequence information to source
    FieldIDRange field_range() const override {
        return source_ ? source_->field_range() : FieldIDRange{};
    }
    
    size_t field_count() const override {
        return source_ ? source_->field_count() : 0;
    }
    
    bool has_field(FieldID id) const override {
        return source_ ? source_->has_field(id) : false;
    }
    
    // Automatically forward field metadata to source
    std::optional<FieldDescriptor> get_descriptor(FieldID id) const override {
        return source_ ? source_->get_descriptor(id) : std::nullopt;
    }
    
    // Automatically propagate hints through the chain
    std::vector<DropoutRegion> get_dropout_hints(FieldID id) const override {
        return source_ ? source_->get_dropout_hints(id) : std::vector<DropoutRegion>{};
    }
    
    std::optional<FieldParityHint> get_field_parity_hint(FieldID id) const override {
        return source_ ? source_->get_field_parity_hint(id) : std::nullopt;
    }
    
    std::optional<FieldPhaseHint> get_field_phase_hint(FieldID id) const override {
        return source_ ? source_->get_field_phase_hint(id) : std::nullopt;
    }
    
    std::optional<ActiveLineHint> get_active_line_hint() const override {
        return source_ ? source_->get_active_line_hint() : std::nullopt;
    }
    
    std::optional<VideoParameters> get_video_parameters() const override {
        return cached_video_params_;
    }
    
    // Automatically propagate observations through the chain
    std::vector<std::shared_ptr<Observation>> get_observations(FieldID id) const override {
        return source_ ? source_->get_observations(id) : std::vector<std::shared_ptr<Observation>>{};
    }
    
    // Automatically propagate audio through the chain
    uint32_t get_audio_sample_count(FieldID id) const override {
        return source_ ? source_->get_audio_sample_count(id) : 0;
    }
    
    std::vector<int16_t> get_audio_samples(FieldID id) const override {
        return source_ ? source_->get_audio_samples(id) : std::vector<int16_t>{};
    }
    
    bool has_audio() const override {
        return source_ ? source_->has_audio() : false;
    }
    
    // Automatically propagate EFM through the chain
    uint32_t get_efm_sample_count(FieldID id) const override {
        return source_ ? source_->get_efm_sample_count(id) : 0;
    }
    
    std::vector<uint8_t> get_efm_samples(FieldID id) const override {
        return source_ ? source_->get_efm_samples(id) : std::vector<uint8_t>{};
    }
    
    bool has_efm() const override {
        return source_ ? source_->has_efm() : false;
    }
    
    // Automatically propagate dual-channel access through the chain
    bool has_separate_channels() const override {
        return source_ ? source_->has_separate_channels() : false;
    }
    
    const sample_type* get_line_luma(FieldID id, size_t line) const override {
        return source_ ? source_->get_line_luma(id, line) : nullptr;
    }
    
    const sample_type* get_line_chroma(FieldID id, size_t line) const override {
        return source_ ? source_->get_line_chroma(id, line) : nullptr;
    }
    
    std::vector<sample_type> get_field_luma(FieldID id) const override {
        return source_ ? source_->get_field_luma(id) : std::vector<sample_type>{};
    }
    
    std::vector<sample_type> get_field_chroma(FieldID id) const override {
        return source_ ? source_->get_field_chroma(id) : std::vector<sample_type>{};
    }
    
    // Access to wrapped source
    std::shared_ptr<const VideoFieldRepresentation> get_source() const {
        return source_;
    }
    
protected:
    VideoFieldRepresentationWrapper(
        std::shared_ptr<const VideoFieldRepresentation> source,
        ArtifactID id,
        Provenance prov);
    
    std::shared_ptr<const VideoFieldRepresentation> source_;
    std::optional<VideoParameters> cached_video_params_;
};

using VideoFieldRepresentationPtr = std::shared_ptr<VideoFieldRepresentation>;

} // namespace orc
