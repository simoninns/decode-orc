/*
 * File:        video_field_representation.h
 * Module:      orc-core
 * Purpose:     Video field representation interface
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#pragma once

#include <field_id.h>
#include "artifact.h"
#include "dropout_decision.h"
#include "video_metadata_types.h"
#include <orc_source_parameters.h>
#include "../hints/field_parity_hint.h"
#include "../hints/pal_phase_hint.h"
#include "../hints/active_line_hint.h"
// Note: pal_phase_hint.h contains FieldPhaseHint (works for both PAL and NTSC)
#include <common_types.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>
#include <memory>

namespace orc {

// Forward declarations

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

// ============================================================================
// Field Height Calculation Utilities
// ============================================================================
// These utilities implement standards-compliant field height calculations
// for VFR (Video Field Representation) conversion and TBC file I/O.
//
// Standards:
// - NTSC: 525 total lines per frame
//   * First field (even): 262 lines
//   * Second field (odd): 263 lines
// - PAL: 625 total lines per frame
//   * First field (odd): 312 lines
//   * Second field (even): 313 lines
//
// The VFR represents the internal format (no padding), while TBC files
// use padded fields (both fields same length). These utilities handle both.

/**
 * @brief Calculate standards-compliant field height (VFR representation)
 *
 * Returns the actual number of lines in a field according to broadcast standards,
 * without any padding. This is the height stored in VFR descriptors.
 *
 * @param system Video system (NTSC, PAL, etc.)
 * @param is_first_field True if this is the first field in temporal order
 * @return Number of active lines in this field (no padding)
 *
 * Examples:
 * - NTSC, first field: 262 lines
 * - NTSC, second field: 263 lines
 * - PAL, first field: 312 lines
 * - PAL, second field: 313 lines
 */
inline size_t calculate_standard_field_height(VideoSystem system, bool is_first_field) {
    switch (system) {
        case VideoSystem::NTSC:
        case VideoSystem::PAL_M:
            // NTSC: Even field (first) = 262 lines, Odd field (second) = 263 lines
            return is_first_field ? 262 : 263;
            
        case VideoSystem::PAL:
            // PAL: Odd field (first) = 312 lines, Even field (second) = 313 lines
            return is_first_field ? 312 : 313;
            
        case VideoSystem::Unknown:
        default:
            // Unknown system - should not happen in normal operation
            return 0;
    }
}

/**
 * @brief Calculate padded field height (TBC file format)
 *
 * Returns the field height as stored in TBC files. TBC files use padded fields
 * where both fields have equal length. Padding is added to the first field
 * (in temporal order) to equalize lengths.
 *
 * Used only by sink stages when writing TBC files for ld-decode compatibility.
 *
 * @param system Video system (NTSC, PAL, etc.)
 * @return Padded field height as stored in TBC files
 *
 * Examples:
 * - NTSC: 263 lines (padding added to first field)
 * - PAL: 313 lines (padding added to first field)
 */
inline size_t calculate_padded_field_height(VideoSystem system) {
    switch (system) {
        case VideoSystem::NTSC:
        case VideoSystem::PAL_M:
            // NTSC TBC files: both fields stored as 263 lines
            // (first field has 1 line of padding added)
            return 263;
            
        case VideoSystem::PAL:
            // PAL TBC files: both fields stored as 313 lines
            // (first field has 1 line of padding added)
            return 313;
            
        case VideoSystem::Unknown:
        default:
            // Unknown system - should not happen in normal operation
            return 0;
    }
}

// ============================================================================
// Field/Frame Coordinate Conversion Utilities
// ============================================================================
// These utilities convert between field coordinates (field index, field line)
// and frame coordinates (frame number, frame line) accounting for interlacing.
//
// Coordinate Systems:
// - Field coordinates: (field_index, field_line_number)
//   * field_index: 0-based sequential field number
//   * field_line_number: 1-based line number within the field (1 to field_height)
//
// - Frame coordinates: (frame_number, frame_line_number)
//   * frame_number: 1-based frame number (field_index / 2 + 1)
//   * frame_line_number: 1-based line number within the frame (1 to 525 for NTSC, 1 to 625 for PAL)
//
// Interlacing Rules:
// - NTSC: First field = lines 1,3,5...; Second field = lines 2,4,6...
//         First field starts at frame line 1, second field starts at frame line 2
//         Second field line offsets: +262 (since first field has 262 lines)
//
// - PAL:  First field = lines 1,3,5...; Second field = lines 2,4,6...
//         First field starts at frame line 1, second field starts at frame line 2
//         Second field line offsets: +312 (since first field has 312 lines)

/**
 * @brief Result of field-to-frame conversion
 */
struct FieldToFrameResult {
    uint64_t frame_number;       ///< 1-based frame number
    int frame_line_number;       ///< 1-based frame line number (1 to 525/625)
    bool is_first_field;         ///< True if this is the first field of the frame
};

/**
 * @brief Convert field coordinates to frame coordinates
 *
 * @param system Video system (NTSC, PAL, PAL_M)
 * @param field_index 0-based field index
 * @param field_line_number 1-based line number within the field (1 to field_height)
 * @return Frame number (1-based) and frame line number (1-based), or nullopt if invalid
 *
 * Examples (NTSC):
 * - field 0, line 1 → frame 1, line 1 (first field)
 * - field 1, line 1 → frame 1, line 263 (second field, offset by 262)
 * - field 2, line 1 → frame 2, line 1 (first field)
 *
 * Examples (PAL):
 * - field 0, line 1 → frame 1, line 1 (first field)
 * - field 1, line 1 → frame 1, line 313 (second field, offset by 312)
 * - field 2, line 1 → frame 2, line 1 (first field)
 */
inline std::optional<FieldToFrameResult> field_to_frame_coordinates(
    VideoSystem system,
    uint64_t field_index,
    int field_line_number)
{
    if (field_line_number < 1) {
        return std::nullopt;  // Invalid line number
    }

    // Determine if this is the first or second field
    bool is_first_field = (field_index % 2) == 0;
    
    // Calculate frame number (1-based)
    uint64_t frame_number = (field_index / 2) + 1;
    
    // Calculate frame line number based on field parity
    int frame_line_number;
    if (is_first_field) {
        // First field: frame lines start at 1
        frame_line_number = field_line_number;
    } else {
        // Second field: frame lines are offset by first field height
        size_t first_field_height = calculate_standard_field_height(system, true);
        frame_line_number = field_line_number + static_cast<int>(first_field_height);
    }
    
    return FieldToFrameResult{frame_number, frame_line_number, is_first_field};
}

/**
 * @brief Result of frame-to-field conversion
 */
struct FrameToFieldResult {
    uint64_t field_index;        ///< 0-based field index
    int field_line_number;       ///< 1-based line number within the field
    bool is_first_field;         ///< True if this is the first field of the frame
};

/**
 * @brief Convert frame coordinates to field coordinates
 *
 * @param system Video system (NTSC, PAL, PAL_M)
 * @param frame_number 1-based frame number
 * @param frame_line_number 1-based frame line number (1 to 525/625)
 * @return Field index (0-based) and field line number (1-based), or nullopt if invalid
 *
 * Examples (NTSC):
 * - frame 1, line 1 → field 0, line 1 (first field)
 * - frame 1, line 263 → field 1, line 1 (second field)
 * - frame 2, line 1 → field 2, line 1 (first field)
 *
 * Examples (PAL):
 * - frame 1, line 1 → field 0, line 1 (first field)
 * - frame 1, line 313 → field 1, line 1 (second field)
 * - frame 2, line 1 → field 2, line 1 (first field)
 */
inline std::optional<FrameToFieldResult> frame_to_field_coordinates(
    VideoSystem system,
    uint64_t frame_number,
    int frame_line_number)
{
    if (frame_number < 1 || frame_line_number < 1) {
        return std::nullopt;  // Invalid inputs
    }

    // Get field heights for this system
    size_t first_field_height = calculate_standard_field_height(system, true);
    size_t second_field_height = calculate_standard_field_height(system, false);
    size_t total_frame_lines = first_field_height + second_field_height;
    
    if (static_cast<size_t>(frame_line_number) > total_frame_lines) {
        return std::nullopt;  // Line number exceeds frame height
    }
    
    // Determine which field this line belongs to
    bool is_first_field = static_cast<size_t>(frame_line_number) <= first_field_height;
    
    // Calculate field index (0-based)
    uint64_t field_index = (frame_number - 1) * 2 + (is_first_field ? 0 : 1);
    
    // Calculate field line number
    int field_line_number;
    if (is_first_field) {
        field_line_number = frame_line_number;
    } else {
        field_line_number = frame_line_number - static_cast<int>(first_field_height);
    }
    
    return FrameToFieldResult{field_index, field_line_number, is_first_field};
}

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
    using sample_type = uint16_t;  // 16-bit samples (standard for video field data)
    
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
    virtual std::optional<SourceParameters> get_video_parameters() const {
        return std::nullopt;  // Default: no parameters
    }
    
    /**
     * @brief Get VBI hint data if available
     * 
     * Returns raw VBI (Vertical Blanking Interval) data extracted from metadata.
     * Only available for TBC sources; returns empty optional for other sources.
     * 
     * @param id Field ID
     * @return VBI data if available, std::nullopt otherwise
     */
    virtual std::optional<VbiData> get_vbi_hint(FieldID /*id*/) const {
        return std::nullopt;  // Default: no VBI data
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
    
    std::optional<SourceParameters> get_video_parameters() const override {
        return cached_video_params_;
    }
    
    std::optional<VbiData> get_vbi_hint(FieldID id) const override {
        return source_ ? source_->get_vbi_hint(id) : std::nullopt;
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
    std::optional<SourceParameters> cached_video_params_;
};

using VideoFieldRepresentationPtr = std::shared_ptr<VideoFieldRepresentation>;

} // namespace orc
