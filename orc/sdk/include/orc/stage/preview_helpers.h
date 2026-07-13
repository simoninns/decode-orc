/*
 * File:        preview_helpers.h
 * Module:      decode-orc Plugin SDK (stage contract)
 * Purpose:     Helper functions for stage preview rendering
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef PREVIEW_HELPERS_H
#define PREVIEW_HELPERS_H

#include <orc/stage/orc_rendering.h>
#include <orc/stage/preview_stage_types.h>
#include <orc/stage/stage_preview_capability.h>
#include <orc/stage/video_frame_representation.h>

#include <memory>
#include <vector>

namespace orc {

/**
 * @brief Renderer-side helper functions for VFR-based stage previews.
 *
 * These utilities provide standard preview rendering for
 * VideoFrameRepresentation data.  They are used both by the preview renderer
 * (directly, for migrated stages) and by IStageCustomPreviewRenderer
 * implementations that handle non-standard multi-output cases.
 */
namespace PreviewHelpers {

/**
 * @brief Build a StagePreviewCapability from a VideoFrameRepresentation.
 *
 * Returns an invalid (default-constructed) capability when @p vfr is null or
 * contains no frames.  The returned capability advertises the appropriate
 * signal-domain VideoDataType based on the VFR's video parameters.
 *
 * @param vfr  The video frame representation produced by the stage.
 * @return     Populated StagePreviewCapability, or is_valid()==false on
 * failure.
 */
StagePreviewCapability make_signal_preview_capability(
    const std::shared_ptr<const VideoFrameRepresentation>& vfr);

/**
 * @brief Generate standard preview options for a VideoFrameRepresentation.
 *
 * Offers "interlaced_clamped", "interlaced_raw", "sequential_clamped",
 * "sequential_raw" options indexed by frame number.
 *
 * @param representation The video frame representation to preview
 * @return Vector of preview options (empty if representation is invalid)
 */
std::vector<PreviewOption> get_standard_preview_options(
    const std::shared_ptr<const VideoFrameRepresentation>& representation);

/**
 * @brief Render a standard preview from a VideoFrameRepresentation.
 *
 * Supports option_ids "interlaced_clamped", "interlaced_raw",
 * "sequential_clamped", "sequential_raw".
 *
 * @param representation The video frame representation
 * @param option_id Preview option identifier
 * @param index Frame index (0-based)
 * @param hint Navigation hint (unused currently)
 * @param mask_inactive_area When true, the full frame is rendered at normal
 * size with the region outside the active picture (active_video_start/end ×
 *        first/last_active_frame_line) dimmed.  Works in both the interlaced
 *        and sequential layouts: the mask maps each display row back to its
 *        weaved (frame-flat) line, so the sequential layout gets one active
 *        band per field.  No cropping or rescaling is applied.
 * @return Preview image (invalid if option unknown or rendering fails)
 */
PreviewImage render_standard_preview(
    const std::shared_ptr<const VideoFrameRepresentation>& representation,
    const std::string& option_id, uint64_t index,
    PreviewNavigationHint hint = PreviewNavigationHint::Random,
    bool mask_inactive_area = false);

}  // namespace PreviewHelpers

}  // namespace orc

#endif  // PREVIEW_HELPERS_H
