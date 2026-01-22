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
#include <node_id.h>
#include <field_id.h>

// Forward declare core Project type
namespace orc {
    class Project;
}

namespace orc::presenters {

// Forward declarations
class Project;

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
 * - Exporting sequences from nodes
 * - Tracking render progress
 * - Managing render cache
 * 
 * The presenter uses the core rendering pipeline but provides
 * a simplified interface suitable for GUI consumption.
 */
class RenderPresenter {
public:
    /**
     * @brief Construct presenter for a project
     * @param project Project to render from
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
    
    // === Preview Rendering ===
    
    /**
     * @brief Render a preview image for a specific field at a node
     * @param node_id Node to render from
     * @param field_id Field to render
     * @return Preview image (empty if failed)
     */
    PreviewImage renderPreview(NodeID node_id, FieldID field_id);
    
    /**
     * @brief Render preview for the first available field
     * @param node_id Node to render from
     * @return Preview image (empty if failed)
     */
    PreviewImage renderPreviewFirst(NodeID node_id);
    
    /**
     * @brief Get the number of fields available at a node
     * @param node_id Node to query
     * @return Number of fields, or 0 if unavailable
     */
    size_t getFieldCount(NodeID node_id) const;
    
    /**
     * @brief Get list of available field IDs at a node
     * @param node_id Node to query
     * @return List of field IDs
     */
    std::vector<FieldID> getAvailableFields(NodeID node_id) const;
    
    // === Sequence Export ===
    
    /**
     * @brief Export a sequence from a node
     * @param node_id Node to export from
     * @param options Export options
     * @return true on success
     */
    bool exportSequence(NodeID node_id, const ExportOptions& options);
    
    /**
     * @brief Cancel an ongoing export
     */
    void cancelExport();
    
    /**
     * @brief Check if export is in progress
     */
    bool isExporting() const;
    
    /**
     * @brief Get current export progress
     */
    RenderProgress getExportProgress() const;
    
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
    
    // === Image Information ===
    
    /**
     * @brief Get image dimensions for a node
     * @param node_id Node to query
     * @param width Output width
     * @param height Output height
     * @return true if dimensions available
     */
    bool getImageDimensions(NodeID node_id, int& width, int& height) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orc::presenters
