/*
 * File:        stage_preview_capability.h
 * Module:      orc-core
 * Purpose:     Capability contracts for stages that expose structured preview data.
 *
 *              Phase 1 of the preview-refactor plan.  Stages opt in to the new
 *              structured preview path by implementing IStagePreviewCapability
 *              alongside their existing stage interfaces.
 *
 *              The IStagePreviewCapability interface supersedes the implicit
 *              capability inference previously done from PreviewOption lists.
 *              The legacy PreviewableStage::render_preview() baked-RGB path will
 *              be retired in Phase 2 when the carrier-backed rendering pipeline
 *              replaces it.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "orc_preview_types.h"

namespace orc {

// =============================================================================
// Navigation Extent
// =============================================================================

/**
 * @brief Describes the navigable range of a stage's preview output.
 *
 * Source/transform stages operate on fields; colour-sink stages typically
 * navigate by frame.  The item_label carries a human-readable unit string so
 * that UI components can label their controls generically (e.g. "Field 42 of
 * 400" vs "Frame 21 of 200").
 */
struct PreviewNavigationExtent {
    uint64_t    item_count{0};          ///< Number of navigable items (> 0 when data is loaded)
    uint32_t    granularity{1};         ///< Minimum navigation step (usually 1; 2 for field-pair stages)
    std::string item_label{"field"};    ///< Human-readable unit (e.g. "field", "frame")

    /**
     * @brief Returns true when this extent represents a non-empty navigable range.
     *
     * A default-constructed extent (item_count == 0) is not valid; stages
     * should only return a valid extent after their data has been loaded.
     */
    bool is_valid() const
    {
        return item_count > 0 && granularity > 0 && !item_label.empty();
    }
};

// =============================================================================
// Preview Geometry
// =============================================================================

/**
 * @brief Active picture dimensions and display aspect ratio for a stage output.
 *
 * Declared once in StagePreviewCapability; used by the rendering infrastructure
 * for aspect-ratio correction and export pixel dimensions.
 *
 * dar_correction_factor is the horizontal scaling factor that maps from active
 * sample width to the intended display width at the declared display_aspect_ratio.
 * For NTSC/PAL broadcast sources this is typically around 0.7 (e.g. 910 samples
 * wide × 0.7 ≈ 637 display pixels for a 4:3 picture at ~227 samples per μs).
 */
struct PreviewGeometry {
    uint32_t active_width{0};               ///< Active picture width in samples
    uint32_t active_height{0};              ///< Active picture height in lines
    double   display_aspect_ratio{4.0/3.0}; ///< Intended display aspect ratio (width/height)
    double   dar_correction_factor{1.0};    ///< Horizontal stretch factor for display

    /**
     * @brief Returns true when all geometry fields are non-zero/positive.
     *
     * A default-constructed geometry (zero dimensions) is not valid.
     */
    bool is_valid() const
    {
        return active_width > 0
            && active_height > 0
            && display_aspect_ratio > 0.0
            && dar_correction_factor > 0.0;
    }
};

// =============================================================================
// Preview-Tweakable Parameters
// =============================================================================

/**
 * @brief Cost class for a preview-tweakable parameter.
 *
 * Determines the update cycle triggered when the user moves a live tweak
 * control inside the preview window:
 *
 * - DisplayPhase: re-apply conversion to the already-decoded cached output
 *   only.  No re-decode is needed.  Example: colorimetric matrix/primaries/
 *   transfer (issue #140).
 *
 * - DecodePhase: apply the updated parameter to the stage instance and
 *   re-decode the current preview field/frame only.  Example: chroma_gain,
 *   chroma_phase, luma_nr, decoder_type, transform_threshold.
 *
 * Output parameters (file path, container format, encoder preset) are never
 * preview-tweakable and must not appear in the tweakable_parameters list.
 */
enum class PreviewTweakClass {
    DisplayPhase,  ///< Conversion-only update; no re-decode
    DecodePhase,   ///< Single-field re-decode; no full encode-to-file
};

/**
 * @brief Declares a single stage parameter as live-tweakable in the preview window.
 *
 * The parameter_name must match the key used in get_parameters() and
 * get_parameter_descriptors() for the same stage so that the live panel can
 * drive the existing ParameterDescriptor metadata without new UI descriptors.
 */
struct PreviewTweakableParameter {
    std::string      parameter_name;                       ///< Key identifying the parameter
    PreviewTweakClass tweak_class{PreviewTweakClass::DecodePhase}; ///< Update cost class
};

// =============================================================================
// Stage Preview Capability
// =============================================================================

/**
 * @brief Complete capability declaration for a stage that exposes preview data.
 *
 * Stages compose this structure after their data is loaded and return it via
 * IStagePreviewCapability::get_preview_capability().
 *
 * supported_data_types lists every VideoDataType this stage can supply.
 * Colour-sink stages should also list their signal-domain input type (e.g.
 * YC_PAL alongside ColourPAL) so that the preview infrastructure can resolve
 * the upstream node's VFR and expose input-side views without per-stage caching.
 *
 * tweakable_parameters may be empty when no live tweaking is supported.
 *
 * is_valid() is false on a default-constructed or pre-load capability; callers
 * must check before using the contained values.
 */
struct StagePreviewCapability {
    std::vector<VideoDataType>           supported_data_types;  ///< Non-empty list of data types this stage can supply
    PreviewNavigationExtent              navigation_extent;      ///< Navigable range description
    PreviewGeometry                      geometry;               ///< Active picture dimensions and DAR
    std::vector<PreviewTweakableParameter> tweakable_parameters; ///< Optional; empty when no live tweaking

    /**
     * @brief Returns true when the capability is fully populated and usable.
     *
     * Requires: at least one supported data type, a valid navigation extent,
     * and a valid geometry.  tweakable_parameters may be empty.
     */
    bool is_valid() const
    {
        return !supported_data_types.empty()
            && navigation_extent.is_valid()
            && geometry.is_valid();
    }
};

// =============================================================================
// IStagePreviewCapability Interface
// =============================================================================

/**
 * @brief Interface for stages that declare structured preview capabilities.
 *
 * Stages opt in by inheriting from IStagePreviewCapability alongside their
 * existing DAGStage/ParameterizedStage/TriggerableStage base classes.  The
 * method should be called after execute() or trigger() has succeeded; before
 * data is loaded get_preview_capability() should return a StagePreviewCapability
 * with an empty supported_data_types and a zero-item navigation_extent (i.e.
 * is_valid() == false).
 *
 * This interface replaces the implicit capability inference that was previously
 * done from PreviewOption lists; the legacy PreviewableStage::render_preview()
 * baked-RGB path will be retired in Phase 2.
 */
class IStagePreviewCapability {
public:
    virtual ~IStagePreviewCapability() = default;

    /**
     * @brief Return this stage's current preview capability declaration.
     *
     * @return StagePreviewCapability populated with the stage's current data.
     *         Returns is_valid() == false when no data is yet loaded.
     */
    virtual StagePreviewCapability get_preview_capability() const = 0;
};

} // namespace orc
