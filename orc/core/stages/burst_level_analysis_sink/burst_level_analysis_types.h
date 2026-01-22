/*
 * File:        burst_level_analysis_types.h
 * Module:      orc-core
 * Purpose:     Burst level analysis types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_BURST_LEVEL_ANALYSIS_TYPES_H
#define ORC_CORE_BURST_LEVEL_ANALYSIS_TYPES_H

#include "field_id.h"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace orc {

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
    int32_t frame_number;                 ///< Frame number (1-based)
    double median_burst_ire = 0.0;        ///< Average burst level from both fields (IRE)
    bool has_data = false;                ///< True if at least one field had data
    size_t field_count = 0;               ///< Number of fields with data (for averaging)
};

} // namespace orc

#endif // ORC_CORE_BURST_LEVEL_ANALYSIS_TYPES_H
