/*
 * File:        pal_tbc_converter.h
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     PAL TBC level mapping and frame assembly into CVBS_U10_4FSC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cvbs_signal_constants.h>
#include <frame_descriptor.h>

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
// TBC field ordering note (design §5.2.3 / EBU Tech. 3280-E §1.3):
//   ld-decode stores the odd (earlier temporal) field as TBC field 1 with
//   312 lines, and the even field as TBC field 2 with 313 lines.
//   CVBS convention places the 313-line block first in the flat frame buffer.
//   Therefore: TBC field 2 (313 lines) → CVBS field 1;
//               TBC field 1 (312 lines) → CVBS field 2.
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
  // tbc_field1: kPalField1Lines-1 = 312 lines × 1135 samples from the TBC
  //   file (the odd/earlier temporal field — ld-decode TBC field 1).
  //   Only the first (kPalFrameLines - kPalField1Lines) × field_width = 312
  //   lines are used; the caller must pass exactly 312 × field_width samples.
  //
  // tbc_field2: kPalField1Lines = 313 lines × 1135 samples from the TBC file
  //   (the even/later temporal field — ld-decode TBC field 2).
  //
  // tbc_blanking / tbc_white: TBC-domain level values.
  //
  // Output layout:
  //   [CVBS field 1: 313 lines (= TBC field 2)] followed by
  //   [CVBS field 2: 312 lines (= TBC field 1)]
  //   with 2 extra bridge samples on line 312 and 2 on line 624 (EBU3280).
  //   Total output size = kPalFrameSamples = 709,379.
  static std::vector<int16_t> assemble_frame(
      const std::vector<uint16_t>& tbc_field1,  // 312 × 1135 samples
      const std::vector<uint16_t>& tbc_field2,  // 313 × 1135 samples
      int32_t tbc_blanking, int32_t tbc_white);

  // -------------------------------------------------------------------------
  // Colour frame sequence
  // -------------------------------------------------------------------------

  // EBU Tech. 3280-E §1.1.1: map a TBC field_phase_id to the FrameDescriptor
  // colour_frame_index (1–4 for PAL).
  //
  // TBC ld-decode encodes PAL phase as a field-level attribute.  Two
  // consecutive fields share the same colour frame: the mapping is:
  //   field_phase_id ∈ {1,2} → colour_frame_index 1
  //   field_phase_id ∈ {3,4} → colour_frame_index 2
  //   field_phase_id ∈ {5,6} → colour_frame_index 3
  //   field_phase_id ∈ {7,8} → colour_frame_index 4
  // Returns -1 when field_phase_id is absent or out of range.
  static int map_field_phase_to_colour_frame_index(
      std::optional<int32_t> field_phase_id);

 private:
  // Insert one linearly-interpolated extra sample at the end of a line buffer.
  // EBU Tech. 3280-E §1.3.1: the extra sample is the midpoint between the
  // last sample of this line and the first sample of the next line.
  static int16_t interpolate_extra_sample(int16_t last_on_line,
                                          int16_t first_on_next_line);
};

}  // namespace orc
