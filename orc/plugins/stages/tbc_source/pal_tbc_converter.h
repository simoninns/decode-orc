/*
 * File:        pal_tbc_converter.h
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     PAL TBC level mapping and frame assembly into CVBS_U10_4FSC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/frame_descriptor.h>

#include <cstdint>
#include <vector>

namespace orc {

// ---------------------------------------------------------------------------
// PalTBCConverter
// ---------------------------------------------------------------------------
// Stateless helper class for PAL TBC → CVBS_U10_4FSC conversion.
//
// EBU Tech. 3280-E §1.1: PAL 4FSC sampling.
// EBU Tech. 3280-E §1.3: PAL non-orthogonal line structure.
//
// TBC field ordering note (EBU Tech. 3280-E §1.3 / ld-decode PAL convention):
//   PAL field 1 (odd scan, first temporal, isFirstField=true) has 313 stored
//   lines (lines 1, 3, 5, …, 625). Field 2 (even scan, second temporal) has
//   312 stored lines (lines 2, 4, …, 624). EBU/CVBS convention places field 1
//   first in the flat frame buffer. Therefore:
//     TBC field 1 (313 lines) → CVBS field 1 (top, odd scan);
//     TBC field 2 (312 lines) → CVBS field 2 (bottom, even scan).
class PalTBCConverter {
 public:
  // -------------------------------------------------------------------------
  // Level mapping
  // -------------------------------------------------------------------------

  // EBU Tech. 3280-E: map one TBC 16-bit unsigned sample to CVBS_U10_4FSC.
  //
  // tbc_blanking and tbc_white are the TBC-domain level values read from
  // `.tbc.json.db` (blanking_16b_ire, white_16b_ire).  No output clamping:
  // headroom below sync tip and above peak white is preserved in the int16_t
  // result.
  static int16_t tbc_to_cvbs(uint16_t tbc_sample, int32_t tbc_blanking,
                             int32_t tbc_white);

  // -------------------------------------------------------------------------
  // Frame assembly
  // -------------------------------------------------------------------------

  // Assemble a CVBS_U10_4FSC PAL frame from two TBC fields.
  //
  // tbc_field1: kPalField1Lines = 313 lines × 1135 samples from the TBC file
  //   (the odd/earlier temporal field — ld-decode TBC field 1, isFirstField).
  //
  // tbc_field2: kPalFrameLines - kPalField1Lines = 312 lines × 1135 samples
  //   from the TBC file (even/later temporal — ld-decode TBC field 2).
  //
  // tbc_blanking / tbc_white: TBC-domain level values.
  //
  // Output layout:
  //   [CVBS field 1: 313 lines (= TBC field 1, odd scan)] followed by
  //   [CVBS field 2: 312 lines (= TBC field 2, even scan)]
  //   with 2 extra bridge samples on line 312 and 2 on line 624 (EBU3280).
  //   Total output size = kPalFrameSamples = 709,379.
  static std::vector<int16_t> assemble_frame(
      const std::vector<uint16_t>& tbc_field1,  // 313 × 1135 samples
      const std::vector<uint16_t>& tbc_field2,  // 312 × 1135 samples
      int32_t tbc_blanking, int32_t tbc_white);
};

}  // namespace orc
