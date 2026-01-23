/*
 * File:        ntsc_observation_view_models.h
 * Module:      orc-presenters
 * Purpose:     NTSC observation view models for MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <optional>

namespace orc::presenters {

/**
 * @brief FM Code observation data (NTSC line 10)
 */
struct FMCodeView {
    bool present = false;              ///< Whether FM code is present
    int32_t data_value = 0;            ///< 20-bit data value
    bool field_flag = false;           ///< Field flag bit
};

/**
 * @brief White Flag observation data (NTSC line 11)
 */
struct WhiteFlagView {
    bool present = false;              ///< Whether white flag is present
};

/**
 * @brief NTSC observations for a single field
 */
struct NtscFieldObservationsView {
    std::optional<FMCodeView> fm_code;        ///< FM code (line 10)
    std::optional<WhiteFlagView> white_flag;  ///< White flag (line 11)
};

} // namespace orc::presenters
