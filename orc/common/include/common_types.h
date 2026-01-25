/*
 * File:        common_types.h
 * Module:      orc-common
 * Purpose:     Common type definitions shared across all layers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <optional>
#include <field_id.h>

namespace orc {

// Re-export core types for convenience
// These are defined in their respective headers
// This file just provides a single include point for common types

// ============================================================================
// Video system and source type enums
// ============================================================================

/**
 * @brief Video format/system
 */
enum class VideoSystem {
    PAL,    // 625-line PAL
    NTSC,   // 525-line NTSC
    PAL_M,  // 525-line PAL
    Unknown
};

/// Source type for parameter filtering
enum class SourceType {
    Unknown,     // Source type not determined yet
    Composite,   // Composite source (Y+C modulated together, e.g., .tbc files)
    YC           // YC source (separate Y and C channels, e.g., .tbcy/.tbcc files)
};

// ============================================================================
// Preview and rendering types
// ============================================================================

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

// ============================================================================
// Analysis mode enums
// ============================================================================

/**
 * @brief Mode for SNR analysis
 */
enum class SNRAnalysisMode {
    WHITE,  ///< Analyze white SNR only
    BLACK,  ///< Analyze black PSNR only
    BOTH    ///< Analyze both white SNR and black PSNR
};

/**
 * @brief Mode for dropout analysis
 */
enum class DropoutAnalysisMode {
    FULL_FIELD,    ///< Analyze dropouts across the entire field
    VISIBLE_AREA   ///< Analyze dropouts only in the visible area
};

// ============================================================================
// Analysis result types - shared between core and GUI/CLI layers
// ============================================================================

/**
 * @brief Dropout statistics for a single field
 */
struct FieldDropoutStats {
    FieldID field_id;
    double total_dropout_length = 0.0;   ///< Total dropout length in samples
    size_t dropout_count = 0;             ///< Number of dropout regions
    std::optional<int32_t> frame_number;  ///< Frame number if available from VBI
    bool has_data = false;                ///< True if dropout data was successfully extracted
};

/**
 * @brief Dropout statistics aggregated for a frame (two fields)
 */
struct FrameDropoutStats {
    int32_t frame_number;                 ///< Frame number (1-based)
    double total_dropout_length = 0.0;    ///< Total dropout length summed in this bucket (samples)
    double dropout_count = 0.0;           ///< Total dropout count summed in this bucket
    bool has_data = false;                ///< True if at least one frame contributed data
};

/**
 * @brief SNR statistics for a single field
 */
struct FieldSNRStats {
    FieldID field_id;
    double white_snr = 0.0;               ///< White SNR value (dB)
    double black_psnr = 0.0;              ///< Black PSNR value (dB)
    bool has_white_snr = false;           ///< True if white SNR data is available
    bool has_black_psnr = false;          ///< True if black PSNR data is available
    std::optional<int32_t> frame_number;  ///< Frame number if available from VBI
    bool has_data = false;                ///< True if any SNR data was successfully extracted
};

/**
 * @brief SNR statistics aggregated for a frame (two fields)
 */
struct FrameSNRStats {
    int32_t frame_number = 0;             ///< Frame number (1-based)
    double white_snr = 0.0;               ///< Average white SNR (dB)
    double black_psnr = 0.0;              ///< Average black PSNR (dB)
    bool has_white_snr = false;           ///< True if white SNR data is available
    bool has_black_psnr = false;          ///< True if black PSNR data is available
    bool has_data = false;                ///< True if at least one field had data
    size_t field_count = 0;               ///< Number of fields with data (for averaging)
};

/**
 * @brief Burst level statistics for a single field
 */
struct FieldBurstLevelStats {
    FieldID field_id;
    double median_burst_ire = 0.0;        ///< Median burst level in IRE
    std::optional<int32_t> frame_number;  ///< Frame number if available from VBI
    bool has_data = false;                ///< True if burst level data was successfully extracted
};

/**
 * @brief Burst level statistics aggregated for a frame (two fields)
 */
struct FrameBurstLevelStats {
    int32_t frame_number = 0;             ///< Frame number (1-based)
    double median_burst_ire = 0.0;        ///< Average burst level from both fields (IRE)
    bool has_data = false;                ///< True if at least one field had data
    size_t field_count = 0;               ///< Number of fields with data (for averaging)
};

} // namespace orc
