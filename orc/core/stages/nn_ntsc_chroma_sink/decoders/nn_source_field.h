/*
 * File:        nn_source_field.h
 * Module:      orc-core
 * Purpose:     Self-contained source field and video parameter types for the
 *              NN NTSC chroma decoder.  Decoupled from ld-decode-tools and
 *              orc::SourceParameters — the VfrToSourceFieldAdapter (Phase 3)
 *              translates between the two at the stage boundary.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2019 Adam Sampson
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#pragma once

#include <cstdint>
#include <vector>

namespace orc::nn_ntsc {

// Minimal video parameter struct for the NN NTSC decoder.
// Fields mirror LdDecodeMetaData::VideoParameters from ld-decode-tools but are
// entirely independent of that library.  Populated by VfrToSourceFieldAdapter.
struct NnVideoParameters {
    int32_t fieldWidth          = 0;
    int32_t fieldHeight         = 0;
    int32_t colourBurstStart    = 0;
    int32_t colourBurstEnd      = 0;
    int32_t activeVideoStart    = 0;
    int32_t activeVideoEnd      = 0;
    int32_t firstActiveFrameLine = 0;
    int32_t lastActiveFrameLine  = 0;

    // IRE reference levels in 16-bit unsigned sample units
    double white16bIre  = 54400.0;  // 100 IRE — typical NTSC 16-bit
    double black16bIre  =  9600.0;  //   0 IRE

    // Sample rate and subcarrier frequency (Hz)
    double sampleRate = 4.0 * 3579545.0;  // 4×fSC NTSC
    double fSC        = 3579545.0;        // NTSC colour subcarrier

    bool isWidescreen = false;
};

// A field with metadata and raw 16-bit per-sample composite video data.
// Populated by VfrToSourceFieldAdapter (Phase 3) from the stage's
// VideoFieldRepresentation input.
struct NnSourceField {
    int32_t             seq_no       = 0;
    bool                is_first_field = true;
    int32_t             field_phase_id = 0;  // PAL phase ID; 0 = unknown/NTSC
    std::vector<uint16_t> data;              // Raw 16-bit composite samples

    // Return the vertical offset of this field within the interlaced frame
    // (0 for the first/top field, 1 for the second/bottom field).
    int32_t getOffset() const {
        return is_first_field ? 0 : 1;
    }

    // Return the first active line index within this field's sample data.
    int32_t getFirstActiveLine(const NnVideoParameters &vp) const {
        return (vp.firstActiveFrameLine + 1 - getOffset()) / 2;
    }

    // Return the last active line index within this field's sample data.
    int32_t getLastActiveLine(const NnVideoParameters &vp) const {
        return (vp.lastActiveFrameLine + 1 - getOffset()) / 2;
    }
};

}  // namespace orc::nn_ntsc
