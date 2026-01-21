/*
 * File:        output_backend.h
 * Module:      orc-core
 * Purpose:     Abstract output backend for chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_OUTPUT_BACKEND_H
#define ORC_CORE_OUTPUT_BACKEND_H

#include <string>
#include <map>
#include <memory>
#include "tbc_metadata.h"

// Forward declaration
class ComponentFrame;

namespace orc {

class VideoFieldRepresentation;  // Forward declaration for audio access

/**
 * @brief Abstract base class for output backends
 * 
 * Provides interface for writing decoded video frames to various formats.
 * Implementations include raw file output and FFmpeg-based encoding.
 */
class OutputBackend {
public:
    virtual ~OutputBackend() = default;
    
    /**
     * @brief Configuration for output backend
     */
    struct Configuration {
        std::string output_path;              ///< Output file path
        orc::VideoParameters video_params;    ///< Video parameters from decoder
        int padding_amount = 8;               ///< Padding for codec requirements
        std::map<std::string, std::string> options;  ///< Format-specific options
        
        // Crop parameters (applied when reading from ComponentFrame)
        int crop_left = 0;                    ///< Pixels to crop from left
        int crop_top = 0;                     ///< Lines to crop from top
        int crop_width = 0;                   ///< Target width after crop (0 = no crop)
        int crop_height = 0;                  ///< Target height after crop (0 = no crop)
        
        // Encoder quality settings (for FFmpeg backends)
        std::string encoder_preset = "medium";  ///< Encoder preset: fast, medium, slow, veryslow
        int encoder_crf = 18;                   ///< Constant Rate Factor (0-51, lower=better)
        int encoder_bitrate = 0;                ///< Bitrate in bits/sec (0 = use CRF)
        
        // Audio settings
        bool embed_audio = false;               ///< Embed audio in output (requires audio data)
        const class VideoFieldRepresentation* vfr = nullptr;  ///< VFR for audio access (if embed_audio=true)
        uint64_t start_field_index = 0;         ///< Starting field for audio extraction
        uint64_t num_fields = 0;                ///< Number of fields to extract audio from
        
        // Closed caption settings (MP4 only, converts EIA-608 to mov_text)
        bool embed_closed_captions = false;     ///< Embed closed captions as mov_text subtitle (MP4 only)
        const class ObservationContext* observation_context = nullptr;  ///< Observation context with CC data (if embed_closed_captions=true)
    };
    
    /**
     * @brief Initialize the output backend
     * 
     * Opens output file, initializes encoder/writer, and prepares for frame writing.
     * 
     * @param config Output configuration
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize(const Configuration& config) = 0;
    
    /**
     * @brief Write a decoded frame to output
     * 
     * @param frame Component frame to write
     * @return true if write successful, false otherwise
     */
    virtual bool writeFrame(const ComponentFrame& frame) = 0;
    
    /**
     * @brief Finalize output and close file
     * 
     * Flushes any buffered data, writes trailers, and closes output file.
     * 
     * @return true if finalization successful, false otherwise
     */
    virtual bool finalize() = 0;
    
    /**
     * @brief Get human-readable format information
     * 
     * @return String describing the output format (for logging)
     */
    virtual std::string getFormatInfo() const = 0;
};

/**
 * @brief Factory for creating output backends
 */
class OutputBackendFactory {
public:
    /**
     * @brief Create appropriate backend for given format
     * 
     * @param format Output format string (e.g., "rgb", "mp4-h264")
     * @return Unique pointer to backend, or nullptr if format unknown
     */
    static std::unique_ptr<OutputBackend> create(const std::string& format);
    
    /**
     * @brief Get list of supported output formats
     * 
     * @return Vector of format strings
     */
    static std::vector<std::string> getSupportedFormats();
};

} // namespace orc

#endif // ORC_CORE_OUTPUT_BACKEND_H
