/*
 * File:        dropout_analysis_types.h
 * Module:      orc-core
 * Purpose:     Dropout analysis types and enums
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_DROPOUT_ANALYSIS_TYPES_H
#define ORC_CORE_DROPOUT_ANALYSIS_TYPES_H

#include <field_id.h>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace orc {

/**
 * @brief Mode for dropout analysis
 */
enum class DropoutAnalysisMode {
    FULL_FIELD,    ///< Analyze dropouts across the entire field
    VISIBLE_AREA   ///< Analyze dropouts only in the visible area
};

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

} // namespace orc

#endif // ORC_CORE_DROPOUT_ANALYSIS_TYPES_H
