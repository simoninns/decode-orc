/*
 * File:        video_field_representation.h
 * Module:      orc-core
 * Purpose:     Video field representation interface
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include "field_id.h"
#include "artifact.h"
#include "dropout_decision.h"
#include "tbc_metadata.h"
#include "../hints/field_parity_hint.h"
#include "../hints/pal_phase_hint.h"
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
    
    std::optional<VideoParameters> get_video_parameters() const override {
        return cached_video_params_;
    }
    
    // Automatically propagate observations through the chain
    std::vector<std::shared_ptr<Observation>> get_observations(FieldID id) const override {
        return source_ ? source_->get_observations(id) : std::vector<std::shared_ptr<Observation>>{};
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
