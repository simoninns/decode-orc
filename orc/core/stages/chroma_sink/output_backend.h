/*
 * File:        output_backend.h
 * Module:      orc-core
 * Purpose:     Abstract output backend for chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
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
        bool active_area_only = false;        ///< Output only active area without padding
        std::map<std::string, std::string> options;  ///< Format-specific options
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
