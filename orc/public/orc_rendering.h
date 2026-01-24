/*
 * File:        orc_rendering.h
 * Module:      orc-public
 * Purpose:     Public API for rendering and preview types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <map>
#include <common_types.h>  // For PreviewOutputType, AspectRatioMode
#include <node_id.h>
#include <field_id.h>

namespace orc {
namespace public_api {

/**
 * @brief Represents a dropout region in a field
 * 
 * A dropout is a region where the video signal was lost or corrupted during
 * capture. This structure identifies the location and extent of the dropout.
 */
struct DropoutRegion {
    enum class DetectionBasis {
        SAMPLE_DERIVED,   ///< Detected from signal analysis
        HINT_DERIVED,     ///< From decoder hints
        CORROBORATED      ///< Both sample and hint agree
    };
    
    uint32_t line;              ///< Line number (0-based)
    uint32_t start_sample;      ///< Start sample within line
    uint32_t end_sample;        ///< End sample within line (exclusive)
    DetectionBasis basis;       ///< How this dropout was detected
    
    DropoutRegion() 
        : line(0), start_sample(0), end_sample(0)
        , basis(DetectionBasis::HINT_DERIVED) {}
};

/**
 * @brief Rendered preview image data
 * 
 * Simple RGB888 image format for GUI display.
 * All rendering logic (sample scaling, field weaving, etc.) is done in core.
 */
struct PreviewImage {
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> rgb_data;  ///< RGB888 format (width * height * 3 bytes)
    std::vector<DropoutRegion> dropout_regions;  ///< Dropout regions to highlight
    
    bool is_valid() const {
        return !rgb_data.empty() && rgb_data.size() == width * height * 3;
    }
};

/**
 * @brief Result of rendering a preview
 */
struct PreviewRenderResult {
    PreviewImage image;
    bool success;
    std::string error_message;
    NodeID node_id;
    PreviewOutputType output_type;
    uint64_t output_index;          ///< Which output was rendered (field N, frame N, etc.)
    
    bool is_valid() const {
        return success && image.is_valid();
    }
};

/**
 * @brief Progress information for batch rendering operations
 */
struct RenderProgress {
    int current_field;
    int total_fields;
    std::string status_message;
    bool is_complete;
    bool has_error;
    std::string error_message;
    
    /// Calculate percentage complete (0-100)
    int percentage() const {
        if (total_fields == 0) return 0;
        return static_cast<int>((current_field * 100) / total_fields);
    }
};

/**
 * @brief Information about an available output type for preview
 */
struct PreviewOutputInfo {
    PreviewOutputType type;
    std::string display_name;       ///< Human-readable name
    uint64_t count;                 ///< Number of outputs available (e.g., 100 fields, 50 frames)
    bool is_available;              ///< Whether this type is available for this node
    double dar_aspect_correction;   ///< Width scaling factor for 4:3 DAR (e.g., 0.7 for PAL/NTSC)
    std::string option_id;          ///< Original option ID from PreviewableStage (for direct rendering)
    bool dropouts_available;        ///< Whether dropout highlighting is available for this output type
    bool has_separate_channels;     ///< Whether source has separate Y/C channels (for signal dropdown)
};

/**
 * @brief Detailed information for displaying an item in preview
 * 
 * Provides all components needed for GUI to arrange labels as desired.
 */
struct PreviewItemDisplayInfo {
    std::string type_name;          ///< Type name (e.g., "Field", "Frame", "Frame (Reversed)")
    uint64_t current_number;        ///< Current item number (1-based)
    uint64_t total_count;           ///< Total number of items available
    uint64_t first_field_number;    ///< First field number (1-based, 0 if N/A)
    uint64_t second_field_number;   ///< Second field number (1-based, 0 if N/A)
    bool has_field_info;            ///< True if field numbers are relevant
};

/**
 * @brief Information about an aspect ratio mode option
 */
struct AspectRatioModeInfo {
    AspectRatioMode mode;
    std::string display_name;       ///< Human-readable name for GUI
    double correction_factor;       ///< Width scaling factor (1.0 for SAR, 0.7 for DAR)
};

/**
 * @brief Result of querying for suggested view node
 */
struct SuggestedViewNode {
    NodeID node_id;                 ///< Node to view (invalid if none available)
    bool has_nodes;                 ///< True if DAG has any nodes at all
    std::string message;            ///< User-facing message explaining the situation
    
    /// Helper to check if a valid node was suggested
    bool is_valid() const { return node_id.is_valid(); }
};

/**
 * @brief VBI data decoded from a field
 */
struct VBIData {
    bool has_data;                  ///< True if VBI data was successfully decoded
    std::optional<int32_t> frame_number;  ///< Frame number if available
    std::string raw_data;           ///< Raw VBI data as string (for display)
    
    bool is_valid() const { return has_data; }
};

/**
 * @brief Observation data for a specific field
 * 
 * Contains metadata and analysis results for a rendered field.
 */
struct ObservationData {
    bool has_data;                  ///< True if observation data is available
    std::string stage_name;         ///< Name of the stage that provided this data
    std::map<std::string, std::string> metadata;  ///< Key-value metadata
    
    bool is_valid() const { return has_data; }
};

/**
 * @brief Result of navigating to next/previous line in frame mode
 * 
 * When displaying a frame with two interlaced fields, moving up/down navigates
 * between alternating fields. This structure tells you which field and line to
 * fetch next.
 */
struct FrameLineNavigationResult {
    bool is_valid;                  ///< True if navigation succeeded (within bounds)
    uint64_t new_field_index;       ///< Field index to render next
    int new_line_number;            ///< Line number to render next (within the field)
};

} // namespace public_api
} // namespace orc
