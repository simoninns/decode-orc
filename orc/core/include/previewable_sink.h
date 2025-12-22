/*
 * File:        previewable_sink.h
 * Module:      orc-core
 * Purpose:     Interface for sink stages that support preview rendering
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "field_id.h"
#include <memory>

namespace orc {

/**
 * @brief Interface for sink stages that can render previews
 * 
 * Sink stages normally have no outputs (they write to disk, display, etc.).
 * However, some sinks may apply transformations (resize, colorize, etc.)
 * before writing, and users may want to preview what the output will look like.
 * 
 * Sinks that implement this interface can provide a transformed field
 * representation for preview purposes, allowing the GUI to show what
 * the sink would output without actually triggering the full export.
 * 
 * This is an OPTIONAL interface - sinks that don't support previews
 * simply don't implement it, and PreviewRenderer will handle them
 * appropriately (return empty outputs).
 * 
 * Design:
 * - Sink receives input from its connected node
 * - render_preview_field() applies the sink's transformation to that input
 * - PreviewRenderer uses the transformed representation for display
 * - No changes to GUI code needed - all handled in core
 * 
 * Example use cases:
 * - Resizing sink: Preview shows the resized output
 * - Colorizing sink: Preview shows the colorized output
 * - Format conversion sink: Preview shows the converted format
 * - Standard sink (no transform): Can return input unchanged for convenience
 */
class PreviewableSink {
public:
    virtual ~PreviewableSink() = default;
    
    /**
     * @brief Render a preview of what this sink would output for a field
     * 
     * Takes the input field representation and applies any transformations
     * that the sink would perform when writing/exporting.
     * 
     * @param input The input field representation (from connected node)
     * @param field_id The specific field to preview
     * @return Transformed field representation showing what sink would output
     * 
     * Note: This method should be const and stateless - it should not
     * modify the sink's state or perform actual export operations.
     * It's purely for preview visualization.
     * 
     * Implementation notes:
     * - For sinks with no transformation: return input unchanged
     * - For resizing sinks: apply resize transformation
     * - For color sinks: apply colorization
     * - Should be fast enough for interactive preview (called on every frame change)
     */
    virtual std::shared_ptr<const VideoFieldRepresentation> render_preview_field(
        std::shared_ptr<const VideoFieldRepresentation> input,
        FieldID field_id
    ) const = 0;
    
    /**
     * @brief Check if this sink supports preview rendering
     * 
     * @return True if preview rendering is available and functional
     * 
     * Default implementation returns true. Override if preview support
     * is conditional (e.g., depends on parameters or capabilities).
     */
    virtual bool supports_preview() const { return true; }
};

} // namespace orc
