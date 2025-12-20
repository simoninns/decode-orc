/*
 * File:        preview_renderer.h
 * Module:      orc-core
 * Purpose:     Preview rendering for GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "dag_field_renderer.h"
#include "video_field_representation.h"
#include "field_id.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace orc {

/**
 * @brief Output types available for preview
 */
enum class PreviewOutputType {
    Field,              ///< Single field (interlaced)
    Frame,              ///< Frame with natural field order (using is_first_field)
    Frame_Reversed,     ///< Frame with reversed field order
    Split,              ///< Frame with fields stacked vertically (first field on top, second on bottom)
    Luma,               ///< Luma component only
    Chroma,             ///< Chroma component only (future)
    Composite          ///< Composite video (future)
};

/**
 * @brief Aspect ratio display modes
 */
enum class AspectRatioMode {
    SAR_1_1,           ///< Sample Aspect Ratio 1:1 (square pixels, no correction)
    DAR_4_3            ///< Display Aspect Ratio 4:3 (corrected for non-square pixels)
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
    std::string node_id;            ///< Node to view (empty if none available)
    bool has_nodes;                 ///< True if DAG has any nodes at all
    std::string message;            ///< User-facing message explaining the situation
    
    /// Helper to check if a valid node was suggested
    bool is_valid() const { return !node_id.empty(); }
};

/**
 * @brief Information about an available output type
 */
struct PreviewOutputInfo {
    PreviewOutputType type;
    std::string display_name;       ///< Human-readable name
    uint64_t count;                 ///< Number of outputs available (e.g., 100 fields, 50 frames)
    bool is_available;              ///< Whether this type is available for this node
    double dar_aspect_correction;   ///< Width scaling factor for 4:3 DAR (e.g., 0.7 for PAL/NTSC)
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
    std::string node_id;
    PreviewOutputType output_type;
    uint64_t output_index;          ///< Which output was rendered (field N, frame N, etc.)
};

/**
 * @brief Preview renderer for GUI
 * 
 * This class handles ALL rendering logic for the GUI:
 * - Queries available output types at a node
 * - Renders specific outputs (field N, frame N, etc.) to RGB888
 * - Handles field weaving for frames
 * - Handles sample scaling (16-bit TBC -> 8-bit RGB)
 * - Future: chroma decoding, composite generation
 * 
 * The GUI is responsible ONLY for:
 * - Displaying the RGB888 data
 * - User interaction (selecting node, output type, index)
 * - Aspect ratio correction for display
 * 
 * Thread safety: Not thread-safe. Use from single thread only.
 */
class PreviewRenderer {
public:
    /**
     * @brief Construct a preview renderer
     * @param dag The DAG to render previews from
     */
    explicit PreviewRenderer(std::shared_ptr<const DAG> dag);
    
    ~PreviewRenderer() = default;
    
    // Prevent copying
    PreviewRenderer(const PreviewRenderer&) = delete;
    PreviewRenderer& operator=(const PreviewRenderer&) = delete;
    
    // Allow moving
    PreviewRenderer(PreviewRenderer&&) = default;
    PreviewRenderer& operator=(PreviewRenderer&&) = default;
    
    // ========================================================================
    // Query API
    // ========================================================================
    
    /**
     * @brief Get available output types for a node
     * 
     * @param node_id The node to query
     * @return Vector of output info, or empty if node doesn't exist
     * 
     * Example output:
     * - Field: 400 fields available
     * - Frame (Even-Odd): 200 frames available
     * - Frame (Odd-Even): 200 frames available
     * - Luma: 400 fields available
     */
    std::vector<PreviewOutputInfo> get_available_outputs(const std::string& node_id);
    
    /**
     * @brief Get the count of outputs for a specific type
     * 
     * @param node_id The node to query
     * @param type The output type
     * @return Number of outputs, or 0 if type not available
     */
    uint64_t get_output_count(const std::string& node_id, PreviewOutputType type);
    
    // ========================================================================
    // Render API
    // ========================================================================
    
    /**
     * @brief Render a specific output
     * 
     * @param node_id The node to render from
     * @param type The output type (field, frame, etc.)
     * @param index The output index (0-based)
     * @return Rendered image result
     * 
     * Examples:
     * - render_output("node_1", PreviewOutputType::Field, 100) -> field 100
     * - render_output("node_1", PreviewOutputType::Frame_EvenOdd, 50) -> frame 50 (even first)
     */
    PreviewRenderResult render_output(
        const std::string& node_id,
        PreviewOutputType type,
        uint64_t index
    );
    
    /**
     * @brief Update the DAG reference
     * 
     * Call this when the DAG changes (nodes added/removed/modified).
     * This will invalidate any cached render results.
     * 
     * @param dag The new DAG to use
     */
    void update_dag(std::shared_ptr<const DAG> dag);
    
    /**
     * @brief Set the aspect ratio display mode
     * @param mode The aspect ratio mode to use (SAR 1:1 or DAR 4:3)
     */
    void set_aspect_ratio_mode(AspectRatioMode mode);
    
    /**
     * @brief Get the current aspect ratio display mode
     * @return The current aspect ratio mode
     */
    AspectRatioMode get_aspect_ratio_mode() const;
    
    /**
     * @brief Get available aspect ratio modes
     * @return Vector of available aspect ratio mode options
     */
    std::vector<AspectRatioModeInfo> get_available_aspect_ratio_modes() const;
    
    /**
     * @brief Get current aspect ratio mode information
     * @return Info about the currently selected aspect ratio mode
     */
    AspectRatioModeInfo get_current_aspect_ratio_mode_info() const;
    
    /**
     * @brief Convert an index from one output type to equivalent index in another type
     * 
     * @param from_type The current output type
     * @param from_index The current index in from_type
     * @param to_type The target output type to convert to
     * @return The equivalent index in to_type
     * 
     * Examples:
     * - Frame 50 -> Field: returns 100 (first field of frame 50)
     * - Field 100 -> Frame: returns 50 (frame containing field 100)
     * - Frame 50 -> Frame Reversed: returns 50 (same frame, different field order)
     */
    uint64_t get_equivalent_index(
        PreviewOutputType from_type,
        uint64_t from_index,
        PreviewOutputType to_type
    ) const;
    
    /**
     * @brief Get formatted display label for current preview item
     * 
     * @param type The output type being displayed
     * @param index The current index (0-based)
     * @param total_count The total number of items available
     * @return Formatted string for display (e.g., "Frame 62 (124-125) / 250")
     * 
     * Examples:
     * - Field 100 / 500: "Field 101 / 500"
     * - Frame 62 / 250: "Frame 63 (125-126) / 250"
     * - Frame Reversed 62 / 250: "Frame (Reversed) 63 (126-125) / 250"
     */
    std::string get_preview_item_label(
        PreviewOutputType type,
        uint64_t index,
        uint64_t total_count
    ) const;
    
    /**
     * @brief Get suggested node for viewing
     * 
     * Returns the node ID that should be displayed by default.
     * Also provides context about why a particular node was chosen
     * or why no node is available.
     * 
     * Logic (in priority order):
     * 1. First SOURCE node (most common case - view the input)
     * 2. First node with outputs (fallback)
     * 3. No suitable nodes (message explains why)
     * 
     * @return Suggestion with node_id, status, and user message
     */
    SuggestedViewNode get_suggested_view_node() const;
    
    // ========================================================================
    // Export API
    // ========================================================================
    
    /**
     * @brief Save rendered output to PNG file
     * 
     * @param node_id The node to render from
     * @param type The output type (field, frame, etc.)
     * @param index The output index (0-based)
     * @param filename Path to PNG file to create
     * @return true if successful, false on error
     * 
     * Example:
     * - save_png("node_1", PreviewOutputType::Frame_EvenOdd, 50, "/tmp/frame50.png")
     */
    bool save_png(
        const std::string& node_id,
        PreviewOutputType type,
        uint64_t index,
        const std::string& filename
    );
    
    /**
     * @brief Save a PreviewImage directly to PNG file
     * 
     * @param image The rendered image to save
     * @param filename Path to PNG file to create
     * @return true if successful, false on error
     */
    bool save_png(const PreviewImage& image, const std::string& filename);
    
private:
    /// DAG field renderer for getting field representations
    std::unique_ptr<DAGFieldRenderer> field_renderer_;
    
    /// Current DAG reference
    std::shared_ptr<const DAG> dag_;
    
    /// Current aspect ratio display mode
    AspectRatioMode aspect_ratio_mode_ = AspectRatioMode::DAR_4_3;
    
    // ========================================================================
    // Internal rendering functions
    // ========================================================================
    
    /**
     * @brief Render a single field to RGB888
     */
    PreviewImage render_field(
        std::shared_ptr<const VideoFieldRepresentation> repr,
        FieldID field_id
    );
    
    /**
     * @brief Render a frame (two fields woven together) to RGB888
     * 
     * @param even_first If true, even field on even lines; if false, odd field on even lines
     */
    PreviewImage render_frame(
        std::shared_ptr<const VideoFieldRepresentation> repr,
        FieldID field_a,
        FieldID field_b,
        bool even_first
    );
    
    /**
     * @brief Render a frame by stacking two fields vertically
     * @param repr The video field representation
     * @param field_a First field ID (top)
     * @param field_b Second field ID (bottom)
     * @return Rendered split frame image
     */
    PreviewImage render_split_frame(
        std::shared_ptr<const VideoFieldRepresentation> repr,
        FieldID field_a,
        FieldID field_b
    );
    
    /**
     * @brief Apply aspect ratio scaling to an image
     * 
     * @param input The input image at native TBC resolution
     * @return Scaled image if DAR 4:3 mode, otherwise returns input unchanged
     */
    PreviewImage apply_aspect_ratio_scaling(const PreviewImage& input) const;
    
    /**
     * @brief Convert 16-bit TBC samples to 8-bit grayscale
     * 
     * Applies proper scaling based on black/white IRE levels.
     * Default: simple 16->8 bit shift, but could be improved with metadata.
     */
    uint8_t tbc_sample_to_8bit(uint16_t sample);
};

} // namespace orc
