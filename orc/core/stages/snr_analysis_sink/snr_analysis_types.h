/*
 * File:        snr_analysis_types.h
 * Module:      orc-core
 * Purpose:     SNR analysis types and enums
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_SNR_ANALYSIS_TYPES_H
#define ORC_CORE_SNR_ANALYSIS_TYPES_H

#include <field_id.h>
#include <common_types.h>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace orc {

// Use SNRAnalysisMode from common_types.h

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
    int32_t frame_number;                 ///< Frame number (1-based)
    double white_snr = 0.0;               ///< Average white SNR (dB)
    double black_psnr = 0.0;              ///< Average black PSNR (dB)
    bool has_white_snr = false;           ///< True if white SNR data is available
    bool has_black_psnr = false;          ///< True if black PSNR data is available
    bool has_data = false;                ///< True if at least one field had data
    size_t field_count = 0;               ///< Number of fields with data (for averaging)
};

} // namespace orc

#endif // ORC_CORE_SNR_ANALYSIS_TYPES_H
