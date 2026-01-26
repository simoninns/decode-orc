/*
 * File:        vbi_view_models.h
 * Module:      orc-presenters
 * Purpose:     View-facing VBI data models for GUI/CLI layers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace orc::presenters {

struct CLVTimecodeView {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int picture_number = 0;
};

enum class VbiSoundModeView {
    STEREO = 0,
    MONO = 1,
    AUDIO_SUBCARRIERS_OFF = 2,
    BILINGUAL = 3,
    STEREO_STEREO = 4,
    STEREO_BILINGUAL = 5,
    CROSS_CHANNEL_STEREO = 6,
    BILINGUAL_BILINGUAL = 7,
    MONO_DUMP = 8,
    STEREO_DUMP = 9,
    BILINGUAL_DUMP = 10,
    FUTURE_USE = 11,
    UNKNOWN = 255
};

struct ProgrammeStatusView {
    bool cx_enabled = false;
    bool is_12_inch = true;
    bool is_side_1 = true;
    bool has_teletext = false;
    bool is_digital = false;
    VbiSoundModeView sound_mode = VbiSoundModeView::STEREO;
    bool is_fm_multiplex = false;
    bool is_programme_dump = false;
    bool parity_valid = false;
};

struct Amendment2StatusView {
    bool copy_permitted = false;
    bool is_video_standard = false;
    VbiSoundModeView sound_mode = VbiSoundModeView::STEREO;
};

struct VBIFieldInfoView {
    int field_id = -1;  // 0-indexed
    std::array<int32_t, 3> vbi_data{ { -1, -1, -1 } };

    std::optional<int32_t> picture_number;
    std::optional<CLVTimecodeView> clv_timecode;
    std::optional<int32_t> chapter_number;

    bool stop_code_present = false;
    bool lead_in = false;
    bool lead_out = false;
    std::optional<std::string> user_code;

    std::optional<ProgrammeStatusView> programme_status;
    std::optional<Amendment2StatusView> amendment2_status;

    bool has_vbi_data = false;
    std::string error_message;
};

} // namespace orc::presenters
