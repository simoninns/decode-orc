/*
 * File:        dropout_run.h
 * Module:      orc-core
 * Purpose:     Frame-flat dropout descriptor for CVBS_U10_4FSC frames
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <frame_id.h>

#include <cstdint>

namespace orc {

// Describes a contiguous run of defective samples within a flat frame buffer.
//
// Coordinates are frame-flat: sample_start is a 0-based byte offset from the
// beginning of the frame's sample buffer as returned by
// VideoFrameRepresentation::get_frame(). Use dropout_util functions to convert
// to/from (field, line, sample-within-line) coordinates.
struct DropoutRun {
  FrameID frame_id = 0;

  // 0-based sample offset from the start of the flat frame buffer.
  uint64_t sample_start = 0;

  // Number of consecutive defective samples.
  uint32_t sample_count = 0;

  // Severity on a 0–100 scale (0 = mild signal glitch; 100 = complete loss).
  uint8_t severity = 0;
};

}  // namespace orc
