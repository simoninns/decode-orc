/*
 * File:        previewable_stage.h
 * Module:      orc-core
 * Purpose:     Interface for stages that support preview rendering
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace orc {

// Forward declarations
struct PreviewImage;

/**
 * @brief Hint about how preview frames are being requested
 * 
 * Allows GUI to inform stage whether to optimize for sequential playback
 * or random access (scrubbing)
 */
enum class PreviewNavigationHint {
    Sequential,  ///< Next/Previous buttons - optimize with pre-fetching
    Random       ///< Slider scrubbing - single frame only, no pre-fetch
};

/**
 * @brief Preview option provided by a stage
 * 
 * Each option represents a different way to preview the stage's output
 * (e.g., "Field", "Frame", "Luma Only", etc.)
 */
struct PreviewOption {
    std::string id;                 ///< Unique identifier for this option (e.g., "field", "frame")
    std::string display_name;       ///< Human-readable name for GUI (e.g., "Frame (RGB)")
    bool is_rgb;                    ///< True if rendered as RGB, false if YUV/Luma
    uint32_t width;                 ///< Width of rendered images
    uint32_t height;                ///< Height of rendered images (per field or frame)
    uint64_t count;                 ///< Number of items available (e.g., 100 fields, 50 frames)
    double dar_aspect_correction;   ///< Width scaling factor for 4:3 DAR (typically 0.7 for PAL/NTSC)
};

/**
 * @brief Interface for source and transform stages that can render previews
 * 
 * This interface allows stages to declare what preview options they support
 * and render complete preview images directly. The PreviewRenderer simply
 * displays what the stage provides without additional processing.
 * 
 * Design philosophy:
 * - Stages know best how to preview their own output
 * - Renderer is a dumb display layer that shows what stages provide
 * - Each stage declares available options (field, frame, split, etc.)
 * - Each stage renders complete RGB888 images ready for display
 */
class PreviewableStage {
public:
    virtual ~PreviewableStage() = default;
    
    /**
     * @brief Check if this stage supports preview rendering
     * 
     * @return True if preview is supported and get_preview_options() will return options
     */
    virtual bool supports_preview() const = 0;
    
    /**
     * @brief Get available preview options for this stage
     * 
     * Called by PreviewRenderer to discover what preview modes are available.
     * Should return options based on current stage state (loaded data, parameters, etc.)
     * 
     * @return Vector of preview options (empty if preview not available)
     * 
     * Example for a TBC source:
     * - Field: 400 fields, 1135x313, YUV
     * - Frame: 200 frames, 1135x626, YUV
     * - Frame (Reversed): 200 frames, 1135x626, YUV
     */
    virtual std::vector<PreviewOption> get_preview_options() const = 0;
    
    /**
     * @brief Render a preview image for a specific option and index
     * 
     * Called by PreviewRenderer when GUI requests a preview. Stage should:
     * 1. Validate option_id and index
     * 2. Render the requested content to RGB888
     * 3. Return complete PreviewImage ready for display
     * 
     * @param option_id Option ID from get_preview_options() (e.g., "field", "frame")
     * @param index Item index (0-based, must be < option.count)
     * @param hint Navigation hint - Sequential for buttons, Random for slider (default: Random)
     * @return Rendered preview image (RGB888), invalid if rendering failed
     * 
     * Example:
     * - render_preview("field", 100) -> RGB888 of field 100
     * - render_preview("frame", 50) -> RGB888 of frame 50 (fields 100+101 woven)
     */
    virtual PreviewImage render_preview(const std::string& option_id, uint64_t index,
                                       PreviewNavigationHint hint = PreviewNavigationHint::Random) const = 0;
};

} // namespace orc
