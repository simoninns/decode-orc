/*
 * File:        render_presenter.h
 * Module:      orc-presenters
 * Purpose:     Rendering and preview presenter - MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <node_id.h>
#include <field_id.h>
#include <orc_rendering.h>  // Public API rendering types
#include <common_types.h>   // PreviewOutputType

// Forward declare core types
namespace orc {
    class Project;
    class DAG;
}

namespace orc::presenters {

// Forward declarations
class Project;

/**
 * @brief Progress callback for batch rendering operations
 * 
 * @param current Current field being rendered
 * @param total Total fields to render
 * @param message Status message
 */
using ProgressCallback = std::function<void(int current, int total, const std::string& message)>;

/**
 * @brief VBI data for a single field
 */
struct VBIData {
    bool has_vbi;
    bool is_clv;
    std::string chapter_number;
    std::string frame_number;
    std::string picture_number;
    std::string picture_stop_code;
    std::string user_code;
    std::vector<std::string> raw_vbi_lines;
};

/**
 * @brief Observation data for debugging/analysis
 */
struct ObservationData {
    bool is_valid;
    std::string json_data;  // JSON representation of observations
};

/**
 * @brief Simple RGB image representation for preview
 */
struct PreviewImage {
    std::vector<uint8_t> data;  ///< RGB data (width * height * 3)
    int width;                   ///< Image width
    int height;                  ///< Image height
    
    bool isValid() const { return !data.empty() && width > 0 && height > 0; }
};

/**
 * @brief Export format options
 */
enum class ExportFormat {
    PNG,
    TIFF,
    FFV1,
    ProRes
};

/**
 * @brief Export options for sequence rendering
 */
struct ExportOptions {
    std::string output_path;        ///< Output file/directory path
    ExportFormat format;             ///< Export format
    int start_field;                 ///< First field to export (-1 for all)
    int end_field;                   ///< Last field to export (-1 for all)
    bool deinterlace;                ///< Whether to deinterlace
    int quality;                     ///< Quality setting (0-100)
};

/**
 * @brief Render progress information
 */
struct RenderProgress {
    size_t current_field;            ///< Current field being rendered
    size_t total_fields;             ///< Total fields to render
    std::string status_message;      ///< Current status
    bool is_complete;                ///< Whether rendering is complete
    bool has_error;                  ///< Whether an error occurred
    std::string error_message;       ///< Error message if any
};

/**
 * @brief RenderPresenter - Manages preview and export rendering
 * 
 * This presenter extracts rendering logic from the GUI layer.
 * It provides a clean interface for:
 * - Rendering preview images for specific nodes/fields
 * - Batch rendering with progress callbacks
 * - VBI data extraction
 * - Analysis data requests (dropout, SNR, burst level)
 * - Managing render cache
 * 
 * The presenter uses the core rendering pipeline but provides
 * a simplified interface suitable for GUI consumption.
 * 
 * Thread safety: Methods are thread-safe when explicitly noted.
 * Preview rendering should be done from a worker thread.
 */
class RenderPresenter {
public:
    /**
     * @brief Construct presenter for a project
     * @param project Project to render from (must outlive this presenter)
     */
    explicit RenderPresenter(orc::Project* project);
    
    /**
     * @brief Destructor
     */
    ~RenderPresenter();
    
    // Disable copy, enable move
    RenderPresenter(const RenderPresenter&) = delete;
    RenderPresenter& operator=(const RenderPresenter&) = delete;
    RenderPresenter(RenderPresenter&&) noexcept;
    RenderPresenter& operator=(RenderPresenter&&) noexcept;
    
    // === DAG Management ===
    
    /**
     * @brief Update the internal DAG from the current project state
     * 
     * Call this whenever the project changes (nodes added/removed/modified).
     * This rebuilds the internal DAG and rendering state.
     * 
     * @return true if DAG was built successfully
     */
    bool updateDAG();
    
    /**
     * @brief Set the DAG directly (for coordination with external DAG management)
     * @param dag Shared pointer to DAG (can be nullptr for empty projects)
     */
    void setDAG(std::shared_ptr<const orc::DAG> dag);
    
    // === Preview Rendering ===
    
    /**
     * @brief Render a preview image for a specific output
     * 
     * @param node_id Node to render from
     * @param output_type Type of output (Field, Frame, Luma, etc.)
     * @param output_index Index of the output (0-based)
     * @param option_id Optional rendering option ID
     * @return Preview render result with RGB image
     * 
     * Thread-safe: Yes (uses internal DAG)
     */
    orc::public_api::PreviewRenderResult renderPreview(
        NodeID node_id,
        orc::PreviewOutputType output_type,
        uint64_t output_index,
        const std::string& option_id = ""
    );
    
    /**
     * @brief Get available output types for a node
     * 
     * @param node_id Node to query
     * @return Vector of available output info
     * 
     * Thread-safe: Yes
     */
    std::vector<orc::public_api::PreviewOutputInfo> getAvailableOutputs(NodeID node_id);
    
    /**
     * @brief Get the count of outputs for a specific type
     * 
     * @param node_id Node to query
     * @param output_type Type of output
     * @return Number of outputs (0 if not available)
     * 
     * Thread-safe: Yes
     */
    uint64_t getOutputCount(NodeID node_id, orc::PreviewOutputType output_type);
    
    /**
     * @brief Save a preview as PNG file
     * 
     * @param node_id Node to render from
     * @param output_type Type of output
     * @param output_index Index of output
     * @param filename Path to save PNG
     * @param option_id Optional rendering option ID
     * @return true on success
     */
    bool savePNG(
        NodeID node_id,
        orc::PreviewOutputType output_type,
        uint64_t output_index,
        const std::string& filename,
        const std::string& option_id = ""
    );
    
    // === VBI Data Extraction ===
    
    /**
     * @brief Get VBI data for a specific field
     * 
     * @param node_id Node to extract from
     * @param field_id Field to decode
     * @return VBI data (has_vbi=false if no VBI found)
     */
    VBIData getVBIData(NodeID node_id, FieldID field_id);
    
    // === Analysis Data Access ===
    
    /**
     * @brief Get dropout analysis data from a sink stage
     * 
     * The node must be a DropoutAnalysisSinkStage that has been triggered.
     * This method abstracts DAG traversal from the GUI layer.
     * 
     * @param node_id Node to get data from
     * @param frame_stats Output vector of frame statistics
     * @param total_frames Output total frames count
     * @return true if data was retrieved successfully
     */
    bool getDropoutAnalysisData(
        NodeID node_id,
        std::vector<void*>& frame_stats,  // Actually vector<FrameDropoutStats>
        int32_t& total_frames
    );
    
    /**
     * @brief Get SNR analysis data from a sink stage
     * 
     * @param node_id Node to get data from
     * @param frame_stats Output vector of frame statistics
     * @param total_frames Output total frames count
     * @return true if data was retrieved successfully
     */
    bool getSNRAnalysisData(
        NodeID node_id,
        std::vector<void*>& frame_stats,  // Actually vector<FrameSNRStats>
        int32_t& total_frames
    );
    
    /**
     * @brief Get burst level analysis data from a sink stage
     * 
     * @param node_id Node to get data from
     * @param frame_stats Output vector of frame statistics
     * @param total_frames Output total frames count
     * @return true if data was retrieved successfully
     */
    bool getBurstLevelAnalysisData(
        NodeID node_id,
        std::vector<void*>& frame_stats,  // Actually vector<FrameBurstLevelStats>
        int32_t& total_frames
    );
    
    /**
     * @brief Request dropout analysis data from a sink node (deprecated - use getDropoutAnalysisData)
     * 
     * The node must be a DropoutAnalysisSinkStage that has been triggered.
     * 
     * @param node_id Node to get data from
     * @param request_id Unique request ID for async tracking
     * @param callback Callback when data is ready
     * @return true if request was queued
     */
    bool requestDropoutData(
        NodeID node_id,
        uint64_t request_id,
        std::function<void(uint64_t id, bool success, const std::string& error)> callback
    );
    
    /**
     * @brief Request SNR analysis data from a sink node (deprecated - use getSNRAnalysisData)
     * 
     * @param node_id Node to get data from
     * @param request_id Unique request ID
     * @param callback Callback when data is ready
     * @return true if request was queued
     */
    bool requestSNRData(
        NodeID node_id,
        uint64_t request_id,
        std::function<void(uint64_t id, bool success, const std::string& error)> callback
    );
    
    /**
     * @brief Request burst level analysis data from a sink node (deprecated - use getBurstLevelAnalysisData)
     * 
     * @param node_id Node to get data from
     * @param request_id Unique request ID
     * @param callback Callback when data is ready
     * @return true if request was queued
     */
    bool requestBurstLevelData(
        NodeID node_id,
        uint64_t request_id,
        std::function<void(uint64_t id, bool success, const std::string& error)> callback
    );
    
    // === Batch Rendering (Triggering) ===
    
    /**
     * @brief Trigger a triggerable stage (start batch processing)
     * 
     * @param node_id Node to trigger
     * @param callback Progress callback
     * @return Request ID for tracking
     */
    uint64_t triggerStage(NodeID node_id, ProgressCallback callback);
    
    /**
     * @brief Cancel ongoing trigger operation
     */
    void cancelTrigger();
    
    /**
     * @brief Check if a trigger is in progress
     * @return true if triggering
     */
    bool isTriggerActive() const;
    
    // === Dropout Visualization ===
    
    /**
     * @brief Enable/disable dropout highlighting in previews
     * @param show Whether to show dropouts
     */
    void setShowDropouts(bool show);
    
    /**
     * @brief Get current dropout highlighting state
     * @return true if showing dropouts
     */
    bool getShowDropouts() const;
    
    // === Coordinate Mapping (for interactive features) ===
    
    /**
     * @brief Map image coordinates to field coordinates
     * 
     * Used for determining which field/line user clicked on in preview.
     * 
     * @param node_id Node being previewed
     * @param output_type Output type being displayed
     * @param output_index Output index being displayed
     * @param image_y Y coordinate in preview image
     * @param image_height Height of preview image
     * @return Mapping result with field index and line number
     */
    struct ImageToFieldMapping {
        bool is_valid;
        uint64_t field_index;
        int field_line;
    };
    
    ImageToFieldMapping mapImageToField(
        NodeID node_id,
        orc::PreviewOutputType output_type,
        uint64_t output_index,
        int image_y,
        int image_height
    );
    
    /**
     * @brief Map field coordinates to image coordinates
     * 
     * @param node_id Node being previewed
     * @param output_type Output type being displayed
     * @param output_index Output index being displayed  
     * @param field_index Field index
     * @param field_line Line within field
     * @param image_height Height of preview image
     * @return Image Y coordinate (is_valid=false if out of bounds)
     */
    struct FieldToImageMapping {
        bool is_valid;
        int image_y;
    };
    
    FieldToImageMapping mapFieldToImage(
        NodeID node_id,
        orc::PreviewOutputType output_type,
        uint64_t output_index,
        uint64_t field_index,
        int field_line,
        int image_height
    );
    
    /**
     * @brief Get which fields comprise a frame
     * 
     * @param node_id Node being queried
     * @param frame_index Frame index
     * @return Field indices (is_valid=false if invalid)
     */
    struct FrameFields {
        bool is_valid;
        uint64_t first_field;
        uint64_t second_field;
    };
    
    FrameFields getFrameFields(NodeID node_id, uint64_t frame_index);
    
    /**
     * @brief Navigate to next/previous line in frame preview
     * 
     * @param node_id Node being previewed
     * @param output_type Output type
     * @param current_field Current field index
     * @param current_line Current line in field
     * @param direction +1 for down, -1 for up
     * @param field_height Height of a single field
     * @return New field index and line number
     */
    struct FrameLineNavigation {
        bool is_valid;
        uint64_t new_field_index;
        int new_line_number;
    };
    
    FrameLineNavigation navigateFrameLine(
        NodeID node_id,
        orc::PreviewOutputType output_type,
        uint64_t current_field,
        int current_line,
        int direction,
        int field_height
    );
    
    // === Line Samples (for waveform display) ===
    
    /**
     * @brief Get 16-bit samples for a specific line
     * 
     * @param node_id Node to render from
     * @param output_type Output type
     * @param output_index Output index
     * @param line_number Line number in the field/frame
     * @param sample_x X coordinate hint (for field selection in frames)
     * @param preview_width Width of preview image (for coordinate mapping)
     * @return Vector of 16-bit sample values
     */
    std::vector<int16_t> getLineSamples(
        NodeID node_id,
        orc::PreviewOutputType output_type,
        uint64_t output_index,
        int line_number,
        int sample_x,
        int preview_width
    );
    
    // === Observations (for debugging) ===
    
    /**
     * @brief Get observation data for a field
     * 
     * @param node_id Node to get observations from
     * @param field_id Field to query
     * @return Observation data as JSON
     */
    ObservationData getObservations(NodeID node_id, FieldID field_id);
    
    // === Cache Management ===
    
    /**
     * @brief Clear the preview cache
     */
    void clearCache();
    
    /**
     * @brief Get cache statistics
     * @return String describing cache usage
     */
    std::string getCacheStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orc::presenters
