/*
 * File:        preview_renderer.h
 * Module:      orc-core
 * Purpose:     Preview rendering for GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "dag_field_renderer.h"
#include "video_field_representation.h"
#include <field_id.h>
#include <node_id.h>
#include "previewable_stage.h"  // For PreviewNavigationHint enum
#include "dropout_decision.h"  // For DropoutRegion
#include "../analysis/vectorscope/vectorscope_data.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

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
    NodeID node_id;            ///< Node to view (invalid if none available)
    bool has_nodes;                 ///< True if DAG has any nodes at all
    std::string message;            ///< User-facing message explaining the situation
    
    /// Helper to check if a valid node was suggested
    bool is_valid() const { return node_id.is_valid(); }
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
 * @brief Rendered preview image data
 * 
 * Simple RGB888 image format for GUI display.
 * All rendering logic (sample scaling, field weaving, etc.) is done in core.
 */
struct PreviewImage {
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> rgb_data;  ///< RGB888 format (width * height * 3 bytes)
    std::optional<VectorscopeData> vectorscope_data;  ///< Optional UV scatter for chroma preview
    std::vector<DropoutRegion> dropout_regions;  ///< Dropout regions for visualization
    
    bool is_valid() const {
        return !rgb_data.empty() && rgb_data.size() == width * height * 3;
    }

    bool has_vectorscope() const {
        return vectorscope_data.has_value() && !vectorscope_data->samples.empty();
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

/**
 * @brief Result of mapping image coordinates to field coordinates
 * 
 * Converts preview image coordinates (x, y) to field-space coordinates,
 * accounting for output type (field/frame/split) and field ordering.
 */
struct ImageToFieldMappingResult {
    bool is_valid;                  ///< True if mapping succeeded
    uint64_t field_index;           ///< Field index for this position
    int field_line;                 ///< Line number within the field
};

/**
 * @brief Result of mapping field coordinates to image coordinates
 * 
 * Converts field-space coordinates back to preview image coordinates.
 * Used for positioning UI elements like cross-hairs.
 */
struct FieldToImageMappingResult {
    bool is_valid;                  ///< True if mapping succeeded
    int image_y;                    ///< Y coordinate in the preview image
};

/**
 * @brief Result of querying which fields make up a frame
 * 
 * Returns the two field indices that comprise a given frame,
 * accounting for field ordering (parity hint).
 */
struct FrameFieldsResult {
    bool is_valid;                  ///< True if query succeeded
    uint64_t first_field;           ///< Index of first field in frame
    uint64_t second_field;          ///< Index of second field in frame
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
    
    /// Current aspect ratio display mode
    AspectRatioMode aspect_ratio_mode_ = AspectRatioMode::DAR_4_3;
    
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
     * @brief Apply aspect ratio scaling to an image
     * 
     * @param input The input image at native TBC resolution
     * @return Scaled image if DAR 4:3 mode, otherwise returns input unchanged
     */
    PreviewImage apply_aspect_ratio_scaling(const PreviewImage& input) const;
    
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
    uint8_t tbc_sample_to_8bit(uint16_t sample);
    
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
