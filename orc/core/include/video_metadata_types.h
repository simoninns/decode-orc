/*
 * File:        video_metadata_types.h
 * Module:      orc-core
 * Purpose:     Video metadata types exposed through VFR interface
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <array>

namespace orc {

/**
 * @brief VBI (Vertical Blanking Interval) data
 * 
 * This structure is exposed through the VFR interface via get_vbi_hint().
 * It contains VBI data that can be used for display or further processing.
 */
struct VbiData {
    bool in_use = false;
    std::array<int32_t, 3> vbi_data = {0, 0, 0};
};

} // namespace orc
