/*
 * File:        pal_tbc_yc_converter.h
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     PAL TBC YC variant — separate luma/chroma frame assembly
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cvbs_signal_constants.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orc {

// ---------------------------------------------------------------------------
// PalTBCYCConverter
// ---------------------------------------------------------------------------
// Extends PAL composite conversion for sources that carry separate luma (Y)
// and chroma (C) TBC files.
//
// Phase alignment check (design §14.11):
//   At open time, compare colour_frame_index at frame 0 for the Y and C
//   channels.  A mismatch indicates the files are mis-paired and must be
//   hard-rejected with a clear error.  Both channels must yield the same
//   colour_frame_index on every frame; if not, the source is unusable.
//
// Frame assembly uses identical level mapping and non-orthogonal line
// insertion as PalTBCConverter::assemble_frame.  The same function is called
// independently for Y and C buffers.
class PalTBCYCConverter {
 public:
  // Check whether luma and chroma channels are aligned at frame 0.
  //
  // Returns true when both colour_frame_index values agree (or when both are
  // -1, meaning unmeasurable).  Returns false when they differ; callers must
  // reject the source with a clear error identifying the phase mismatch.
  static bool check_yc_phase_alignment(int luma_colour_frame_index,
                                       int chroma_colour_frame_index);

  // Returns an error string when the Y/C phase is misaligned, or empty string
  // when aligned.  Use this for the rejection message.
  static std::string yc_alignment_error(int luma_colour_frame_index,
                                        int chroma_colour_frame_index);
};

}  // namespace orc
