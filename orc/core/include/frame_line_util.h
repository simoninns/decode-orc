/*
 * File:        frame_line_util.h
 * Module:      orc-core
 * Purpose:     Per-line sample count and offset helpers for 4FSC CVBS flat
 * frame buffers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <common_types.h>
#include <cvbs_signal_constants.h>

#include <cstddef>
#include <cstdint>

// Free functions for per-line access within a 4FSC CVBS flat frame buffer.
// These are the appropriate primitives for stages that need line-level data;
// VideoFrameRepresentation is a frame-based interface and does not carry
// per-line geometry knowledge.
//
// EBU Tech. 3280-E §1.3.1 (PAL); SMPTE 244M-2003 §4.1 (NTSC / PAL_M).

namespace orc {

// Returns the number of samples in frame-flat line |line| (0-based).
// For PAL, lines listed in kPalExtraSampleLines carry additional samples; each
// occurrence of a line index in that array adds 1 to its count (so lines 312
// and 624, which appear twice, carry spl_nominal + 2 = 1137 samples).
// All other PAL lines and all NTSC/PAL_M lines carry |spl_nominal| samples.
inline size_t frame_line_sample_count(VideoSystem system, size_t spl_nominal,
                                      size_t line) {
  if (system == VideoSystem::PAL) {
    const auto l = static_cast<int32_t>(line);
    size_t extra = 0;
    for (int32_t e : kPalExtraSampleLines) {
      if (e == l) ++extra;
    }
    return spl_nominal + extra;
  }
  return spl_nominal;
}

// Returns the 0-based sample offset of the start of frame-flat line |line|
// within a flat 4FSC CVBS frame buffer.
// O(4) for PAL (iterates kPalExtraSampleLines); O(1) for NTSC and PAL_M.
inline size_t frame_line_sample_offset(VideoSystem system, size_t spl_nominal,
                                       size_t line) {
  if (system != VideoSystem::PAL) return line * spl_nominal;
  // Base: every line contributes spl_nominal (1135) samples.
  size_t offset = line * spl_nominal;
  // Each non-orthogonal line before |line| contributes one extra sample.
  for (int32_t extra : kPalExtraSampleLines) {
    if (extra < static_cast<int32_t>(line)) ++offset;
  }
  return offset;
}

// Converts a frame-flat sample offset to (flat_line, sample_within_line).
// O(1) for NTSC and PAL_M; O(4) for PAL (corrects at most 4 extra-sample
// lines that make simple division inexact).
inline std::pair<size_t, size_t> frame_flat_offset_to_line_sample(
    VideoSystem system, size_t spl_nominal, uint64_t flat_offset) {
  if (system != VideoSystem::PAL) {
    return {static_cast<size_t>(flat_offset / spl_nominal),
            static_cast<size_t>(flat_offset % spl_nominal)};
  }
  // PAL: integer division by spl_nominal gives a good first estimate; correct
  // one step at a time until we find the line that brackets flat_offset.
  size_t est = static_cast<size_t>(flat_offset / spl_nominal);
  for (;;) {
    size_t line_start =
        frame_line_sample_offset(VideoSystem::PAL, spl_nominal, est);
    size_t line_len =
        frame_line_sample_count(VideoSystem::PAL, spl_nominal, est);
    if (flat_offset < line_start) {
      --est;
    } else if (flat_offset >= line_start + line_len) {
      ++est;
    } else {
      return {est, static_cast<size_t>(flat_offset - line_start)};
    }
  }
}

}  // namespace orc
