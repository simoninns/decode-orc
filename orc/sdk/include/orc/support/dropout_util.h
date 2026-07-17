/*
 * File:        dropout_util.h
 * Module:      decode-orc Plugin SDK (stage contract)
 * Purpose:     Frame-flat ↔ field/line/sample coordinate conversion utilities
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

// SDK TIER: stage/dropout — stage contract type crossing the plugin boundary.
// A layout change here bumps the host ABI version.

#include <orc/stage/common_types.h>

#include <cstdint>

// Conversion utilities between the CVBS_U10_4FSC flat frame buffer offset
// used by DropoutRun and the (field, line, sample-within-line) triple used
// in project files and user-facing display.
//
// All conversions are O(1) for NTSC and PAL_M (orthogonal sampling).
// PAL conversions account for the four non-orthogonal 1136-sample lines.
//
// Thread safety: all functions are pure and may be called concurrently.

namespace orc {
namespace dropout_util {

// (field, line, sample) triple — 1-based field index (1 or 2), 0-based line
// within field, 0-based sample within line.
struct FieldLineSample {
  int32_t field;   // 1 = first field, 2 = second field
  int32_t line;    // 0-based line index within field
  int32_t sample;  // 0-based sample index within line
};

// Convert a 0-based flat frame buffer offset to (field, line, sample).
//
// For PAL the function accounts for the four non-orthogonal lines that carry
// 1136 samples using kPalExtraSampleLines from cvbs_signal_constants.h.
// For NTSC and PAL_M sampling is orthogonal and the conversion is O(1).
//
// Behaviour is undefined when sample_offset >= frame_sample_count for the
// given system.
FieldLineSample frame_sample_to_field_line(VideoSystem sys,
                                           uint64_t frame_sample_offset);

// Convert a (field, line, sample) triple to a 0-based flat frame buffer offset.
//
// field must be 1 or 2.  line must be a valid 0-based line index within that
// field.  sample must be a valid 0-based sample index within that line.
//
// For PAL the function accounts for non-orthogonal line lengths.
uint64_t field_line_to_frame_sample(VideoSystem sys, int32_t field,
                                    int32_t line, int32_t sample_within_line);

}  // namespace dropout_util
}  // namespace orc
