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
  const double n =
      static_cast<double>(static_cast<int32_t>(tbc_sample) - tbc_blanking) /
      static_cast<double>(tbc_white - tbc_blanking);
  const double cvbs =
      n * static_cast<double>(kPalWhite - kPalBlanking) + kPalBlanking;
  return static_cast<int16_t>(std::lround(cvbs));
}

// ---------------------------------------------------------------------------
// Private helpers: linear interpolation of extra PAL samples
// ---------------------------------------------------------------------------

int16_t PalTBCConverter::interpolate_extra_sample(int16_t last_on_line,
                                                  int16_t first_on_next_line) {
  // Midpoint interpolation (avoids signed overflow).
  return static_cast<int16_t>((static_cast<int32_t>(last_on_line) +
                               static_cast<int32_t>(first_on_next_line)) /
                              2);
}

// Append 2 linearly-interpolated bridge samples at t=1/3 and t=2/3.
// EBU Tech. 3280-E §1.3.1: the two extra samples on lines 312 and 624 bridge
// the signal from the last nominal sample to the first sample of the next line.
static void append_two_extra_samples(std::vector<int16_t>& buf, int16_t last,
                                     int16_t first_next) {
  const int32_t a = static_cast<int32_t>(last);
  const int32_t b = static_cast<int32_t>(first_next);
  buf.push_back(static_cast<int16_t>((2 * a + b) / 3));
  buf.push_back(static_cast<int16_t>((a + 2 * b) / 3));
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
  constexpr int32_t kLineWidth = kPalSamplesPerLineNominal;           // 1135

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

  // Build flat frame buffer: [CVBS field 1 (313 lines)] [CVBS field 2 (312
  // lines)] with 2 extra interpolated samples appended to the last line of
  // each field (frame-flat lines 312 and 624) per EBU Tech. 3280-E §1.3.1.
  std::vector<int16_t> frame;
  frame.reserve(static_cast<size_t>(kPalFrameSamples));

  // ---- CVBS field 1: sourced from TBC field 2 (313 lines) ----
  for (int32_t line = 0; line < kField2Lines; ++line) {
    const size_t src_start =
        static_cast<size_t>(line) * static_cast<size_t>(kLineWidth);

    frame.insert(
        frame.end(), cvbs2.begin() + static_cast<ptrdiff_t>(src_start),
        cvbs2.begin() + static_cast<ptrdiff_t>(
                            src_start + static_cast<size_t>(kLineWidth)));

    // Frame-flat line 312 (last of field 1) gets 2 extra bridge samples.
    if (line == kField2Lines - 1) {
      const int16_t last_this =
          cvbs2[src_start + static_cast<size_t>(kLineWidth) - 1];
      const int16_t first_next = cvbs1[0];  // first sample of CVBS field 2
      append_two_extra_samples(frame, last_this, first_next);
    }
  }

  // ---- CVBS field 2: sourced from TBC field 1 (312 lines) ----
  for (int32_t line = 0; line < kField1Lines; ++line) {
    const size_t src_start =
        static_cast<size_t>(line) * static_cast<size_t>(kLineWidth);

    frame.insert(
        frame.end(), cvbs1.begin() + static_cast<ptrdiff_t>(src_start),
        cvbs1.begin() + static_cast<ptrdiff_t>(
                            src_start + static_cast<size_t>(kLineWidth)));

    // Frame-flat line 624 (last of field 2) gets 2 extra bridge samples.
    // No following line in this frame; bridge toward the last sample itself.
    if (line == kField1Lines - 1) {
      const int16_t last_this =
          cvbs1[src_start + static_cast<size_t>(kLineWidth) - 1];
      append_two_extra_samples(frame, last_this, last_this);
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
