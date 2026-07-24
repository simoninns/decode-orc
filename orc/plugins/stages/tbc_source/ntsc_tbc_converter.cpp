/*
 * File:        ntsc_tbc_converter.cpp
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     NTSC TBC level mapping and frame assembly into CVBS_U10_4FSC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "ntsc_tbc_converter.h"

#include <cmath>
#include <stdexcept>

namespace orc {

// ---------------------------------------------------------------------------
// Level mapping
// ---------------------------------------------------------------------------

int16_t NtscTBCConverter::tbc_to_cvbs(uint16_t tbc_sample, int32_t tbc_blanking,
                                      int32_t tbc_white) {
  // SMPTE 244M-2003: linear mapping from TBC 16-bit domain to CVBS_U10_4FSC.
  // No output clamping — headroom outside [kNtscSyncTip, kNtscPeak] is
  // preserved.
  const double n =
      static_cast<double>(static_cast<int32_t>(tbc_sample) - tbc_blanking) /
      static_cast<double>(tbc_white - tbc_blanking);
  const double cvbs =
      n * static_cast<double>(kNtscWhite - kNtscBlanking) + kNtscBlanking;
  return static_cast<int16_t>(std::lround(cvbs));
}

// ---------------------------------------------------------------------------
// Frame assembly
// ---------------------------------------------------------------------------

std::vector<int16_t> NtscTBCConverter::assemble_frame(
    const std::vector<uint16_t>& tbc_field1,  // 263 × 910 samples
    const std::vector<uint16_t>& tbc_field2,  // 262 × 910 samples
    int32_t tbc_blanking, int32_t tbc_white) {
  // SMPTE 244M-2003 §4.1 / SMPTE 170M-2004 §11.3: NTSC frame assembly.
  // TBC field ordering (ld-decode convention for NTSC):
  //   TBC field 1 (even file index) = odd-scan/first temporal, 263 real lines
  //     → VFR field 1 (top spatial).  Starts at the very top of the picture.
  //   TBC field 2 (odd file index) = even-scan/second temporal, 262 real lines
  //     → VFR field 2 (bottom spatial).
  // VFR layout: [field 1 (top, 263 lines)][field 2 (bottom, 262 lines)]
  // NTSC is orthogonal: all lines have kNtscSamplesPerLine = 910 samples.
  constexpr int32_t kTBCF1Lines = kNtscField1Lines;                    // 263
  constexpr int32_t kTBCF2Lines = kNtscFrameLines - kNtscField1Lines;  // 262
  constexpr int32_t kLineW = kNtscSamplesPerLine;                      // 910

  const size_t exp_f1 =
      static_cast<size_t>(kTBCF1Lines) * static_cast<size_t>(kLineW);
  const size_t exp_f2 =
      static_cast<size_t>(kTBCF2Lines) * static_cast<size_t>(kLineW);

  if (tbc_field1.size() != exp_f1 || tbc_field2.size() != exp_f2) {
    throw std::invalid_argument(
        "NtscTBCConverter::assemble_frame: unexpected field sample counts");
  }

  std::vector<int16_t> frame;
  frame.reserve(static_cast<size_t>(kNtscFrameSamples));

  // VFR field 1 (top, 263 lines) ← TBC field 1 (odd-scan, first temporal)
  for (const uint16_t s : tbc_field1) {
    frame.push_back(tbc_to_cvbs(s, tbc_blanking, tbc_white));
  }
  // VFR field 2 (bottom, 262 lines) ← TBC field 2 (even-scan, second temporal)
  for (const uint16_t s : tbc_field2) {
    frame.push_back(tbc_to_cvbs(s, tbc_blanking, tbc_white));
  }

  return frame;
}

}  // namespace orc
