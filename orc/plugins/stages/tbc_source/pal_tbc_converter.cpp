/*
 * File:        pal_tbc_converter.cpp
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     PAL TBC level mapping and frame assembly into CVBS_U10_4FSC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "pal_tbc_converter.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace orc {

// ---------------------------------------------------------------------------
// Level mapping
// ---------------------------------------------------------------------------

int16_t PalTBCConverter::tbc_to_cvbs(uint16_t tbc_sample, int32_t tbc_blanking,
                                     int32_t tbc_white) {
  // EBU Tech. 3280-E: linear mapping from TBC 16-bit domain to CVBS_U10_4FSC.
  // No output clamping — headroom outside [kPalSyncTip, kPalPeak] is preserved.
  const double n = static_cast<double>(static_cast<int32_t>(tbc_sample) -
                                       tbc_blanking) /
                   static_cast<double>(tbc_white - tbc_blanking);
  const double cvbs =
      n * static_cast<double>(kPalWhite - kPalBlanking) + kPalBlanking;
  return static_cast<int16_t>(std::lround(cvbs));
}

// ---------------------------------------------------------------------------
// Private helper: linear interpolation of extra PAL sample
// ---------------------------------------------------------------------------

int16_t PalTBCConverter::interpolate_extra_sample(int16_t last_on_line,
                                                  int16_t first_on_next_line) {
  // EBU Tech. 3280-E §1.3.1: midpoint interpolation avoids signed overflow.
  return static_cast<int16_t>(
      (static_cast<int32_t>(last_on_line) +
       static_cast<int32_t>(first_on_next_line)) /
      2);
}

// ---------------------------------------------------------------------------
// Frame assembly
// ---------------------------------------------------------------------------

std::vector<int16_t> PalTBCConverter::assemble_frame(
    const std::vector<uint16_t>& tbc_field1,  // 312 lines × 1135 samples
    const std::vector<uint16_t>& tbc_field2,  // 313 lines × 1135 samples
    int32_t tbc_blanking, int32_t tbc_white) {
  // TBC field ordering (design §5.2.3 / EBU Tech. 3280-E §1.3):
  //   TBC field 1 = odd (earlier temporal), 312 lines → CVBS field 2
  //   TBC field 2 = even (later temporal),  313 lines → CVBS field 1
  constexpr int32_t kField1Lines = kPalFrameLines - kPalField1Lines;  // 312
  constexpr int32_t kField2Lines = kPalField1Lines;                   // 313
  constexpr int32_t kLineWidth = kPalMaxSamplesPerLine - 1;           // 1135

  const size_t expected_field1 =
      static_cast<size_t>(kField1Lines) * static_cast<size_t>(kLineWidth);
  const size_t expected_field2 =
      static_cast<size_t>(kField2Lines) * static_cast<size_t>(kLineWidth);

  if (tbc_field1.size() != expected_field1 ||
      tbc_field2.size() != expected_field2) {
    throw std::invalid_argument(
        "PalTBCConverter::assemble_frame: unexpected field sample counts");
  }

  // Pre-convert all field samples to CVBS domain.
  std::vector<int16_t> cvbs1(expected_field1);
  std::vector<int16_t> cvbs2(expected_field2);

  for (size_t i = 0; i < expected_field1; ++i) {
    cvbs1[i] = tbc_to_cvbs(tbc_field1[i], tbc_blanking, tbc_white);
  }
  for (size_t i = 0; i < expected_field2; ++i) {
    cvbs2[i] = tbc_to_cvbs(tbc_field2[i], tbc_blanking, tbc_white);
  }

  // Build flat frame buffer: [CVBS field 1 (313 lines)] [CVBS field 2 (312 lines)]
  // with 4 extra interpolated samples at kPalExtraSampleLines positions.
  std::vector<int16_t> frame;
  frame.reserve(static_cast<size_t>(kPalFrameSamples));

  // Helper: is frame-flat line index one of the four non-orthogonal lines?
  auto is_extra_line = [](int32_t flat_line) {
    return flat_line == kPalExtraSampleLines[0] ||
           flat_line == kPalExtraSampleLines[1] ||
           flat_line == kPalExtraSampleLines[2] ||
           flat_line == kPalExtraSampleLines[3];
  };

  // ---- CVBS field 1: sourced from TBC field 2 (313 lines) ----
  for (int32_t line = 0; line < kField2Lines; ++line) {
    const int32_t flat_line = line;  // CVBS field 1 occupies frame lines 0..312
    const size_t src_start =
        static_cast<size_t>(line) * static_cast<size_t>(kLineWidth);

    // Write standard 1135 samples.
    frame.insert(frame.end(), cvbs2.begin() + static_cast<ptrdiff_t>(src_start),
                 cvbs2.begin() +
                     static_cast<ptrdiff_t>(src_start + static_cast<size_t>(kLineWidth)));

    if (is_extra_line(flat_line)) {
      // Interpolate between last sample of this line and first of the next.
      const int16_t last_this =
          cvbs2[src_start + static_cast<size_t>(kLineWidth) - 1];
      // For the last line of CVBS field 1 (line 312), the "next" line is the
      // first line of CVBS field 2 (sourced from TBC field 1 line 0).
      const int16_t first_next =
          (line + 1 < kField2Lines)
              ? cvbs2[static_cast<size_t>(line + 1) * static_cast<size_t>(kLineWidth)]
              : cvbs1[0];
      frame.push_back(interpolate_extra_sample(last_this, first_next));
    }
  }

  // ---- CVBS field 2: sourced from TBC field 1 (312 lines) ----
  for (int32_t line = 0; line < kField1Lines; ++line) {
    const int32_t flat_line =
        kField2Lines + line;  // CVBS field 2 occupies frame lines 313..624
    const size_t src_start =
        static_cast<size_t>(line) * static_cast<size_t>(kLineWidth);

    frame.insert(frame.end(), cvbs1.begin() + static_cast<ptrdiff_t>(src_start),
                 cvbs1.begin() +
                     static_cast<ptrdiff_t>(src_start + static_cast<size_t>(kLineWidth)));

    if (is_extra_line(flat_line)) {
      const int16_t last_this =
          cvbs1[src_start + static_cast<size_t>(kLineWidth) - 1];
      // For the last non-orthogonal line of CVBS field 2 (frame line 624),
      // there is no following line in this frame; interpolate with a copy of
      // the last sample (the interpolated sample is the average of the last
      // sample with itself, i.e., the same value).
      const int16_t first_next =
          (line + 1 < kField1Lines)
              ? cvbs1[static_cast<size_t>(line + 1) * static_cast<size_t>(kLineWidth)]
              : last_this;
      frame.push_back(interpolate_extra_sample(last_this, first_next));
    }
  }

  return frame;
}

// ---------------------------------------------------------------------------
// Colour frame sequence
// ---------------------------------------------------------------------------

int PalTBCConverter::map_field_phase_to_colour_frame_index(
    std::optional<int32_t> field_phase_id) {
  // EBU Tech. 3280-E §1.1.1: PAL 4-frame colour sequence.
  //
  // ld-decode encodes the PAL phase as a per-field attribute in the range 1–8
  // (two consecutive fields share the same colour-frame position).  The
  // mapping verified against the FieldPhaseHint convention in
  // tbc_source_internal:
  //
  //   field_phase_id 1,2 → colour_frame_index 1
  //   field_phase_id 3,4 → colour_frame_index 2
  //   field_phase_id 5,6 → colour_frame_index 3
  //   field_phase_id 7,8 → colour_frame_index 4
  if (!field_phase_id.has_value()) {
    return -1;
  }
  const int32_t phase = field_phase_id.value();
  if (phase < 1 || phase > 8) {
    return -1;
  }
  return ((phase - 1) / 2) + 1;  // 1-based, cycles 1-4
}

}  // namespace orc
