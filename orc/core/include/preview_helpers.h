/*
 * File:        preview_helpers.h
 * Module:      orc-core
 * Purpose:     Helper functions for implementing PreviewableStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef PREVIEW_HELPERS_H
#define PREVIEW_HELPERS_H

#include "preview_renderer.h"
#include "previewable_stage.h"
#include <memory>
#include <vector>

namespace orc {

/**
 * @brief Channel selection for YC source preview rendering
 */
enum class RenderChannel {
    COMPOSITE,      // Standard composite (Y+C modulated) - default for composite sources
    LUMA_ONLY,      // Luma channel only (for YC sources)
    CHROMA_ONLY,    // Chroma channel only (for YC sources)
    COMPOSITE_YC    // Y+C combined for visualization (for YC sources)
};

/**
 * @brief Helper functions for stages implementing PreviewableStage interface
 * 
 * These utilities provide standard preview rendering for VideoFieldRepresentation
 * data, eliminating code duplication across source and transform stages.
 */
namespace PreviewHelpers {

/**
 * @brief Generate standard preview options for a VideoFieldRepresentation
 * 
 * Creates 6 standard preview options: Field Y/Raw, Split Y/Raw, Frame Y/Raw
 * 
 * @param representation The video field representation to preview
 * @return Vector of preview options (empty if representation is invalid)
 */
std::vector<PreviewOption> get_standard_preview_options(
    const std::shared_ptr<const VideoFieldRepresentation>& representation);

/**
 * @brief Render a field preview (single field as grayscale)
 * 
 * @param representation The video field representation
 * @param field_id The field to render
 * @param apply_ire_scaling If true, applies IRE black/white level scaling; if false, simple 16→8 bit
 * @return Preview image (invalid if rendering fails)
 */
PreviewImage render_field_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    FieldID field_id,
    bool apply_ire_scaling);

/**
 * @brief Render a split field preview (two fields stacked vertically)
 * 
 * @param representation The video field representation
 * @param pair_index Index of the field pair (0-based)
 * @param apply_ire_scaling If true, applies IRE scaling; if false, simple 16→8 bit
 * @return Preview image (invalid if rendering fails)
 */
PreviewImage render_split_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    uint64_t pair_index,
    bool apply_ire_scaling);

/**
 * @brief Render a frame preview (two fields woven into interlaced frame)
 * 
 * Automatically handles field parity detection and weaving order.
 * 
 * @param representation The video field representation
 * @param frame_index Index of the frame (0-based)
 * @param apply_ire_scaling If true, applies IRE scaling; if false, simple 16→8 bit
 * @return Preview image (invalid if rendering fails)
 */
PreviewImage render_frame_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    uint64_t frame_index,
    bool apply_ire_scaling);

/**
 * @brief Universal preview renderer that dispatches based on option_id
 * 
 * Handles all standard option types: field, field_raw, split, split_raw, frame, frame_raw
 * 
 * @param representation The video field representation
 * @param option_id The preview option identifier
 * @param index The item index (field, pair, or frame depending on option)
 * @param hint Navigation hint for prefetching optimization
 * @return Preview image (invalid if option unknown or rendering fails)
 */
PreviewImage render_standard_preview(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    const std::string& option_id,
    uint64_t index,
    PreviewNavigationHint hint = PreviewNavigationHint::Random);

/**
 * @brief Universal preview renderer with channel selection for YC sources
 * 
 * Handles all standard option types (field, field_raw, split, split_raw, frame, frame_raw)
 * with explicit channel selection for YC sources.
 * 
 * @param representation The video field representation
 * @param option_id The preview option identifier (without channel suffix)
 * @param index The item index (field, pair, or frame depending on option)
 * @param channel Which channel to render (for YC sources)
 * @param hint Navigation hint for prefetching optimization
 * @return Preview image (invalid if option unknown or rendering fails)
 */
PreviewImage render_standard_preview_with_channel(
    const std::shared_ptr<const VideoFieldRepresentation>& representation,
    const std::string& option_id,
    uint64_t index,
    RenderChannel channel,
    PreviewNavigationHint hint = PreviewNavigationHint::Random);

/**
 * @brief Render a field as grayscale with channel selection (for YC sources)
 * 
 * @param representation The video field representation (raw pointer for convenience)
 * @param field_id The field to render
 * @param channel Which channel to render (LUMA_ONLY, CHROMA_ONLY, or COMPOSITE_YC)
 * @param apply_ire_scaling If true, applies IRE scaling; if false, simple 16→8 bit
 * @return Preview image (invalid if rendering fails)
 */
PreviewImage render_field_grayscale(
    const VideoFieldRepresentation* representation,
    FieldID field_id,
    RenderChannel channel = RenderChannel::COMPOSITE,
    bool apply_ire_scaling = true);

} // namespace PreviewHelpers

} // namespace orc

#endif // PREVIEW_HELPERS_H
