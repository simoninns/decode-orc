/*
 * File:        preview_renderer.h
 * Module:      orc-core
 * Purpose:     Preview rendering for GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/include/preview_renderer.h. Use RenderPresenter instead."
#endif
#if defined(ORC_CLI_BUILD)
#error "CLI code cannot include core/include/preview_renderer.h. Use RenderPresenter instead."
#endif

// =============================================================================
// MVP Architecture Enforcement - Phase 3.5 Complete
// =============================================================================
// GUI/CLI code cannot include this file (enforced by compile guards).
// This header is part of core and contains internal implementation types.
//
// GUI/CLI must use:
// - orc/public/*.h for public API types (PreviewImage, etc.)
// - orc/presenters/include/*.h for rendering operations (RenderPresenter)
//
// Presenters bridge between GUI and core, performing type conversions.
// =============================================================================

#include "dag_field_renderer.h"
#include "video_field_representation.h"
#include <field_id.h>
#include <node_id.h>
#include <common_types.h>  // For PreviewOutputType, AspectRatioMode
#include <orc_rendering.h>  // For public API types (DropoutRegion, PreviewImage)
#include "previewable_stage.h"  // For PreviewNavigationHint enum
#include "../analysis/vectorscope/vectorscope_data.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace orc {

// PreviewOutputType and AspectRatioMode now defined in common_types.h

// Use public API DropoutRegion in core (already aliased in dropout_decision.h, but include here for clarity)
using DropoutRegion = orc::DropoutRegion;

// Use view-type structs (defined in orc_rendering.h)
using SuggestedViewNode = orc::SuggestedViewNode;
using PreviewOutputInfo = orc::PreviewOutputInfo;
using PreviewItemDisplayInfo = orc::PreviewItemDisplayInfo;

// Note: PreviewImage and PreviewRenderResult are used directly from view-types
// Use public API mapping result types in core
using FrameLineNavigationResult = orc::FrameLineNavigationResult;
using ImageToFieldMappingResult = orc::ImageToFieldMappingResult;
using FieldToImageMappingResult = orc::FieldToImageMappingResult;
using FrameFieldsResult = orc::FrameFieldsResult;

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
    
    // Move operations deleted - internal renderer contains non-movable components
    PreviewRenderer(PreviewRenderer&&) = delete;
    PreviewRenderer& operator=(PreviewRenderer&&) = delete;
    
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
    std::vector<PreviewOutputInfo> get_available_outputs(const NodeID& node_id);
    
    /**
     * @brief Get the count of outputs for a specific type
     * 
     * @param node_id The node to query
     * @param type The output type
     * @return Number of outputs, or 0 if type not available
     */
    uint64_t get_output_count(const NodeID& node_id, PreviewOutputType type);
    
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
        const NodeID& node_id,
        PreviewOutputType type,
        uint64_t index,
        const std::string& option_id = "",
        PreviewNavigationHint hint = PreviewNavigationHint::Random
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
     * @brief Set whether to render dropout regions onto the image
     * @param show True to render dropouts, false to hide
     */
    void set_show_dropouts(bool show);
    
    /**
     * @brief Get whether dropout rendering is enabled
     * @return True if dropouts are rendered, false otherwise
     */
    bool get_show_dropouts() const;
    
    /**
     * @brief Get the field representation at a node
     * 
     * This allows direct access to the underlying 16-bit field data
     * for operations like line scope display.
     * 
     * @param node_id The node to get representation from
     * @return Shared pointer to the field representation, or nullptr if not available
     */
    std::shared_ptr<const VideoFieldRepresentation> get_representation_at_node(const NodeID& node_id);
    
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
     * @brief Get detailed display information for current preview item
     * 
     * @param type The output type being displayed
     * @param index The current index (0-based)
     * @param total_count The total number of items available
     * @return Display info with all components for GUI to arrange
     * 
     * This provides individual components (type, numbers, range) so the GUI
     * can arrange labels as desired instead of using a pre-formatted string.
     */
    PreviewItemDisplayInfo get_preview_item_display_info(
        PreviewOutputType type,
        uint64_t index,
        uint64_t total_count
    ) const;
    
    /**
     * @brief Navigate to next or previous line within a frame
     * 
     * In frame mode with interlaced fields, this handles the complex logic of
     * toggling between fields and advancing lines. It accounts for the field
     * order (whether field 0 or field 1 is the first field in the frame).
     * 
     * @param node_id The node being displayed
     * @param output_type The current output type (must be Frame or Frame_Reversed)
     * @param current_field The current field index being displayed
     * @param current_line The current line within the field (0-based)
     * @param direction +1 to go down, -1 to go up
     * @param field_height The height of a single field in lines
     * @return Navigation result with new field/line or is_valid=false if out of bounds
     * 
     * Example usage:
     * - User clicks down arrow in line scope dialog
     * - Call navigate_frame_line(..., direction=1)
     * - If is_valid=true, fetch field at new_field_index, line new_line_number
     * - If is_valid=false, stay at current position (at boundary)
     */
    FrameLineNavigationResult navigate_frame_line(
        const NodeID& node_id,
        PreviewOutputType output_type,
        uint64_t current_field,
        int current_line,
        int direction,
        int field_height
    ) const;
    
    /**
     * @brief Map preview image coordinates to field coordinates
     * 
     * Converts an (x, y) position in the rendered preview image to the actual
     * field index and line number, accounting for:
     * - Output type (field/frame/split)
     * - Field ordering (parity hint)
     * - Reversed frame mode
     * 
     * @param node_id The node being displayed
     * @param output_type The output type (Field, Frame, Frame_Reversed, Split)
     * @param output_index The current output index (field/frame number, 0-based)
     * @param image_y The Y coordinate in the preview image
     * @param image_height Total height of the preview image (for split mode)
     * @return Mapping result with field_index and field_line, or is_valid=false
     */
    ImageToFieldMappingResult map_image_to_field(
        const NodeID& node_id,
        PreviewOutputType output_type,
        uint64_t output_index,
        int image_y,
        int image_height = 0
    ) const;
    
    /**
     * @brief Map field coordinates back to preview image coordinates
     * 
     * Converts a (field_index, line_number) position back to the Y coordinate
     * in the rendered preview image. This is the reverse of map_image_to_field.
     * Used for positioning UI elements like cross-hairs.
     * 
     * @param node_id The node being displayed
     * @param output_type The output type (Field, Frame, Frame_Reversed, Split)
     * @param output_index The current output index (field/frame number, 0-based)
     * @param field_index The field index to map
     * @param field_line The line within the field
     * @param image_height Total height of the image (for split mode)
     * @return Mapping result with image_y, or is_valid=false
     */
    FieldToImageMappingResult map_field_to_image(
        const NodeID& node_id,
        PreviewOutputType output_type,
        uint64_t output_index,
        uint64_t field_index,
        int field_line,
        int image_height = 0
    ) const;
    
    /**
     * @brief Get the field indices that make up a frame
     * 
     * Returns which two fields comprise the given frame index,
     * accounting for field ordering (parity hint). This is needed
     * when the GUI wants to display metadata for both fields in a frame.
     * 
     * @param node_id The node being displayed
     * @param frame_index The frame index (0-based)
     * @return Result with first_field and second_field indices
     */
    FrameFieldsResult get_frame_fields(
        const NodeID& node_id,
        uint64_t frame_index
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
     * @param option_id Optional ID for PreviewableStage outputs (default: "")
     * @return true if successful, false on error
     * 
     * Example:
     * - save_png("node_1", PreviewOutputType::Frame_EvenOdd, 50, "/tmp/frame50.png")
     */
    bool save_png(
        const NodeID& node_id,
        PreviewOutputType type,
        uint64_t index,
        const std::string& filename,
        const std::string& option_id = ""
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
    
    /// DAG executor for on-demand execution
    mutable DAGExecutor dag_executor_;
    
    /// Whether to render dropout regions onto images
    bool show_dropouts_ = false;
    
    /// Ensure node has been executed (execute on-demand if needed)
    void ensure_node_executed(const NodeID& node_id, bool disable_cache = false) const;
    
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
     * @brief Render dropout regions onto an image
     * @param image Image to render dropouts onto (modified in place)
     */
    void render_dropouts(PreviewImage& image) const;
    
    /**
     * @brief Convert 16-bit TBC samples to 8-bit grayscale
     * 
     * Applies proper scaling based on black/white IRE levels.
     * Default: simple 16->8 bit shift, but could be improved with metadata.
     */
    uint8_t tbc_sample_to_8bit(uint16_t sample, double blackIRE, double whiteIRE);
    
    // ========================================================================
    // Stage preview support (new interface for sources/transforms)
    // ========================================================================
    
    /**
     * @brief Get available outputs for a previewable stage (source/transform)
     * 
     * @param stage_node_id The stage node ID
     * @param stage_node The DAG node for the stage
     * @param previewable The PreviewableStage interface
     * @return Vector of available output types
     */
    std::vector<PreviewOutputInfo> get_stage_preview_outputs(
        const NodeID& stage_node_id,
        const DAGNode& stage_node,
        const class PreviewableStage& previewable
    );
    
    /**
     * @brief Render preview output from a previewable stage
     * 
     * @param stage_node_id The stage node ID
     * @param stage_node The DAG node for the stage
     * @param previewable The PreviewableStage interface
     * @param type The output type to render
     * @param index The output index
     * @return Rendered preview result
     */
    PreviewRenderResult render_stage_preview(
        const NodeID& stage_node_id,
        const DAGNode& stage_node,
        const class PreviewableStage& previewable,
        PreviewOutputType type,
        uint64_t index,
        const std::string& requested_option_id = "",
        PreviewNavigationHint hint = PreviewNavigationHint::Random
    );
};

} // namespace orc
