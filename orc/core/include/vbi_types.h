/*
 * File:        vbi_types.h
 * Module:      orc-core
 * Purpose:     VBI data types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_VBI_TYPES_H
#define ORC_CORE_VBI_TYPES_H

namespace orc {

// CLV timecode structure
struct CLVTimecode {
    int hours;
    int minutes;
    int seconds;
    int picture_number;
};

// Sound mode enumeration (IEC 60857-1986)
enum class VbiSoundMode {
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
    FUTURE_USE = 11
};

// Programme status information
struct ProgrammeStatus {
    bool cx_enabled = false;          // CX noise reduction on/off
    bool is_12_inch = true;           // Disc size: true=12", false=8"
    bool is_side_1 = true;            // Disc side
    bool has_teletext = false;        // Teletext present
    bool is_digital = false;          // Digital vs analogue video
    VbiSoundMode sound_mode = VbiSoundMode::STEREO;
    bool is_fm_multiplex = false;     // FM-FM multiplex
    bool is_programme_dump = false;   // Programme dump mode
    bool parity_valid = false;        // Parity check passed
};

// Amendment 2 programme status
struct Amendment2Status {
    bool copy_permitted = false;      // Copy permission flag
    bool is_video_standard = false;   // Video signal standard
    VbiSoundMode sound_mode = VbiSoundMode::STEREO;
};

} // namespace orc

#endif // ORC_CORE_VBI_TYPES_H
