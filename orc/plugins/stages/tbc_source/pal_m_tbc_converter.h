/*
 * File:        pal_m_tbc_converter.h
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     PAL_M TBC level mapping and frame assembly into CVBS_U10_4FSC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <orc/stage/cvbs_signal_constants.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace orc {

// ---------------------------------------------------------------------------
// PalMTBCConverter
// ---------------------------------------------------------------------------
// Stateless helper class for PAL_M TBC → CVBS_U10_4FSC conversion.
//
// ITU-R BT.1700-1 Annex 1 Part B: PAL_M is the Brazilian variant of PAL
// that uses NTSC line/field structure (525 lines, 60 Hz) with PAL colour
// encoding.  The subcarrier frequency is ~3.575611 MHz giving 909 samples/line
// at 4FSC.
//
// Signal levels:
//   PAL_M uses the same levels as NTSC (kNtscBlanking, kNtscWhite, etc.)
//   per ITU-R BT.1700-1 Annex 1 Part B.
//
// TBC field ordering (ld-decode convention, identical to NTSC):
//   Both fields stored padded to 263 lines in the TBC file.
//   TBC field 1 (even file index, isFirstField=true) = odd-scan/first temporal,
//     263 real lines → VFR field 1 (top spatial).
//   TBC field 2 (odd file index) = even-scan/second temporal,
//     262 real lines → VFR field 2 (bottom spatial).
//   VFR frame layout: [VFR field 1 (top): 263 × 909]
//                     [VFR field 2 (bottom): 262 × 909]
class PalMTBCConverter {
 public:
  // -------------------------------------------------------------------------
  // Level mapping
  // -------------------------------------------------------------------------

  // ITU-R BT.1700-1 Annex 1 Part B: map one TBC 16-bit unsigned sample to
  // CVBS_U10_4FSC.  Level constants are identical to NTSC (kNtscBlanking,
  // kNtscWhite).  No output clamping.
  static int16_t tbc_to_cvbs(uint16_t tbc_sample, int32_t tbc_blanking,
                             int32_t tbc_white);

  // -------------------------------------------------------------------------
  // Frame assembly
  // -------------------------------------------------------------------------

  // Assemble a CVBS_U10_4FSC PAL_M frame from two TBC fields.
  //
  // tbc_field1: kPalMField1Lines = 263 lines × 909 = 239,067 samples.
  //   Odd-scan/first temporal field (ld-decode even file index); becomes
  //   VFR field 1 (top spatial).
  //
  // tbc_field2: kPalMFrameLines - kPalMField1Lines = 262 lines × 909
  //   = 238,158 samples.  Even-scan/second temporal field (ld-decode odd file
  //   index); becomes VFR field 2 (bottom spatial).  Caller strips the TBC
  //   padding line (stored as 263 lines on disk).
  //
  // PAL_M is orthogonal: kPalMSamplesPerLine = 909 on every line.
  // Output: [VFR field 1 (top): 263 × 909][VFR field 2 (bottom): 262 × 909]
  //         = kPalMFrameSamples.
  static std::vector<int16_t> assemble_frame(
      const std::vector<uint16_t>& tbc_field1,  // 263 × 909 samples
      const std::vector<uint16_t>& tbc_field2,  // 262 × 909 samples
      int32_t tbc_blanking, int32_t tbc_white);
};

}  // namespace orc
