/*
 * File:        dropout_util.cpp
 * Module:      orc-core
 * Purpose:     Frame-flat ↔ field/line/sample coordinate conversion utilities
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <cvbs_signal_constants.h>
#include <dropout_util.h>

#include <algorithm>
#include <stdexcept>

namespace orc {
namespace dropout_util {

// ---------------------------------------------------------------------------
// PAL helpers
// ---------------------------------------------------------------------------

// Returns the number of samples in frame-flat line |flat_line| (0-based).
// kPalExtraSampleLines lines have 1136 samples; all others have 1135.
static int32_t pal_line_samples(int32_t flat_line) {
  for (int32_t extra : kPalExtraSampleLines) {
    if (flat_line == extra) return kPalMaxSamplesPerLine;
  }
  return kPalMaxSamplesPerLine - 1;  // 1135
}

// Returns the cumulative sample offset at the start of frame-flat line
// |flat_line| (0-based).  O(4) because only 4 lines are non-orthogonal.
static uint64_t pal_cumulative_offset(int32_t flat_line) {
  // Base: all lines up to flat_line have 1135 samples.
  uint64_t offset = static_cast<uint64_t>(flat_line) * (kPalMaxSamplesPerLine - 1);
  // Add one extra sample for each non-orthogonal line before flat_line.
  for (int32_t extra : kPalExtraSampleLines) {
    if (extra < flat_line) ++offset;
  }
  return offset;
}

// ---------------------------------------------------------------------------
// frame_sample_to_field_line
// ---------------------------------------------------------------------------

FieldLineSample frame_sample_to_field_line(VideoSystem sys,
                                           uint64_t frame_sample_offset) {
  if (sys == VideoSystem::PAL) {
    // Walk lines using cumulative offsets.  We search for the last line whose
    // cumulative start offset is <= frame_sample_offset.  Binary search would
    // be O(log 625); since this path is only called for dropout metadata
    // loading (not per-sample), a linear scan is acceptable.
    int32_t flat_line = 0;
    uint64_t cumulative = 0;
    for (int32_t l = 0; l < kPalFrameLines; ++l) {
      int32_t line_len = pal_line_samples(l);
      if (cumulative + static_cast<uint64_t>(line_len) > frame_sample_offset) {
        flat_line = l;
        break;
      }
      cumulative += static_cast<uint64_t>(line_len);
      flat_line = l + 1;
    }
    int32_t sample_in_line = static_cast<int32_t>(frame_sample_offset - cumulative);

    // Map flat line to (field, line-within-field).
    int32_t field, line_in_field;
    if (flat_line < kPalField1Lines) {
      field = 1;
      line_in_field = flat_line;
    } else {
      field = 2;
      line_in_field = flat_line - kPalField1Lines;
    }
    return {field, line_in_field, sample_in_line};

  } else if (sys == VideoSystem::NTSC) {
    // Orthogonal: all lines have kNtscSamplesPerLine samples.
    int32_t flat_line =
        static_cast<int32_t>(frame_sample_offset / kNtscSamplesPerLine);
    int32_t sample_in_line =
        static_cast<int32_t>(frame_sample_offset % kNtscSamplesPerLine);
    int32_t field, line_in_field;
    if (flat_line < kNtscField1Lines) {
      field = 1;
      line_in_field = flat_line;
    } else {
      field = 2;
      line_in_field = flat_line - kNtscField1Lines;
    }
    return {field, line_in_field, sample_in_line};

  } else if (sys == VideoSystem::PAL_M) {
    // Orthogonal: all lines have kPalMSamplesPerLine samples.
    int32_t flat_line =
        static_cast<int32_t>(frame_sample_offset / kPalMSamplesPerLine);
    int32_t sample_in_line =
        static_cast<int32_t>(frame_sample_offset % kPalMSamplesPerLine);
    int32_t field, line_in_field;
    if (flat_line < kPalMField1Lines) {
      field = 1;
      line_in_field = flat_line;
    } else {
      field = 2;
      line_in_field = flat_line - kPalMField1Lines;
    }
    return {field, line_in_field, sample_in_line};

  } else {
    return {0, 0, 0};
  }
}

// ---------------------------------------------------------------------------
// field_line_to_frame_sample
// ---------------------------------------------------------------------------

uint64_t field_line_to_frame_sample(VideoSystem sys, int32_t field,
                                    int32_t line, int32_t sample_within_line) {
  if (sys == VideoSystem::PAL) {
    // Convert field-relative line to frame-flat line.
    int32_t flat_line = (field == 1) ? line : kPalField1Lines + line;
    return pal_cumulative_offset(flat_line) +
           static_cast<uint64_t>(sample_within_line);

  } else if (sys == VideoSystem::NTSC) {
    int32_t flat_line = (field == 1) ? line : kNtscField1Lines + line;
    return static_cast<uint64_t>(flat_line) * kNtscSamplesPerLine +
           static_cast<uint64_t>(sample_within_line);

  } else if (sys == VideoSystem::PAL_M) {
    int32_t flat_line = (field == 1) ? line : kPalMField1Lines + line;
    return static_cast<uint64_t>(flat_line) * kPalMSamplesPerLine +
           static_cast<uint64_t>(sample_within_line);

  } else {
    return 0;
  }
}

}  // namespace dropout_util
}  // namespace orc
