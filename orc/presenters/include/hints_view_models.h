/*
 * File:        hints_view_models.h
 * Module:      orc-presenters
 * Purpose:     View-facing hint data models for GUI/CLI layers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>

namespace orc::presenters {

/**
 * @brief Source of a hint as exposed to presentation layers.
 */
enum class HintSourceView {
    METADATA,
    USER_OVERRIDE,
    INHERITED,
    SAMPLE_ANALYSIS,
    CORROBORATED,
    UNKNOWN
};

struct FieldParityHintView {
    bool is_first_field = false;
    HintSourceView source = HintSourceView::UNKNOWN;
    int confidence_pct = 0;
};

struct FieldPhaseHintView {
    int field_phase_id = -1;   // -1 means unknown
    HintSourceView source = HintSourceView::UNKNOWN;
    int confidence_pct = 0;
};

struct ActiveLineHintView {
    int first_active_frame_line = -1;
    int last_active_frame_line = -1;
    HintSourceView source = HintSourceView::UNKNOWN;
    int confidence_pct = 0;

    bool is_valid() const {
        return first_active_frame_line >= 0 &&
               last_active_frame_line >= first_active_frame_line;
    }
};

struct VideoParametersView {
    int active_video_start = -1;
    int active_video_end = -1;
    int colour_burst_start = -1;
    int colour_burst_end = -1;
    int white_16b_ire = -1;
    int blanking_16b_ire = -1;
    int black_16b_ire = -1;
    double sample_rate = 0.0;
};

} // namespace orc::presenters
