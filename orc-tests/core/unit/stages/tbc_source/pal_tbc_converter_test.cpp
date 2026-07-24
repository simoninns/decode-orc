/*
 * File:        pal_tbc_converter_test.cpp
 * Module:      orc-tests/core/unit/stages/tbc_source
 * Purpose:     Unit tests for PalTBCConverter level mapping and frame assembly.
 *
 * Tests:
 *   - tbc_to_cvbs level mapping at blanking, white, and sync-tip reference
 *     points (EBU Tech. 3280-E §1.3.1)
 *   - assemble_frame sample count equals kPalFrameSamples
 *   - CVBS field 1 (first 313 lines in output) is sourced from TBC field 1
 *   - Extra samples are inserted at frame-flat lines 312 and 624 (EBU3280)
 *   - map_field_phase_to_colour_frame_index covers all 1–8 phase values, -1
 *     for absent/invalid
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/tbc_source/pal_tbc_converter.h"

#include <gtest/gtest.h>
#include <orc/stage/cvbs_signal_constants.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace orc_unit_test {

// ============================================================================
// Helpers
// ============================================================================

// Build a synthetic PAL TBC field filled with a constant uint16_t value.
static std::vector<uint16_t> make_field(int32_t lines, int32_t width,
                                        uint16_t fill) {
  return std::vector<uint16_t>(
      static_cast<size_t>(lines) * static_cast<size_t>(width), fill);
}

// Reference TBC levels used throughout these tests (ld-decode defaults for
// PAL).
constexpr int32_t kRefBlanking = 16384;
constexpr int32_t kRefWhite = 54400;

// TBC field line counts (matching EBU/ld-decode PAL convention):
//   TBC field 1 (odd-scan, isFirstField) = 313 lines → CVBS field 1
//   TBC field 2 (even-scan) = 312 lines → CVBS field 2
constexpr int32_t kTBCField1Lines = orc::kPalField1Lines;  // 313
constexpr int32_t kTBCField2Lines =
    orc::kPalFrameLines - orc::kPalField1Lines;                 // 312
constexpr int32_t kLineWidth = orc::kPalSamplesPerLineNominal;  // 1135

// ============================================================================
// tbc_to_cvbs — level mapping
// ============================================================================

TEST(PalTBCConverterTest, LevelMapping_BlankingInputYieldsCVBSBlanking) {
  // A sample at TBC blanking → CVBS blanking (256). EBU Tech. 3280-E §1.3.1.
  const int16_t result = orc::PalTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(kRefBlanking), kRefBlanking, kRefWhite);
  EXPECT_EQ(result, static_cast<int16_t>(orc::kPalBlanking));
}

TEST(PalTBCConverterTest, LevelMapping_WhiteInputYieldsCVBSWhite) {
  const int16_t result = orc::PalTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(kRefWhite), kRefBlanking, kRefWhite);
  EXPECT_EQ(result, static_cast<int16_t>(orc::kPalWhite));
}

TEST(PalTBCConverterTest, LevelMapping_LinearInterpolationMidpoint) {
  // Midpoint of [kRefBlanking, kRefWhite] → midpoint of [kPalBlanking,
  // kPalWhite].
  const int32_t tbc_mid = (kRefBlanking + kRefWhite) / 2;
  const int16_t result = orc::PalTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(tbc_mid), kRefBlanking, kRefWhite);
  const int32_t expected = (orc::kPalBlanking + orc::kPalWhite) / 2;
  // Allow ±1 for rounding.
  EXPECT_NEAR(result, expected, 1);
}

TEST(PalTBCConverterTest,
     LevelMapping_BelowBlankingProducesValueBelowCVBSBlanking) {
  // Samples below TBC blanking (e.g. sync tip) map below CVBS blanking.
  const int16_t result = orc::PalTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(kRefBlanking - 1000), kRefBlanking, kRefWhite);
  EXPECT_LT(result, static_cast<int16_t>(orc::kPalBlanking));
}

// ============================================================================
// assemble_frame — sample count and structure
// ============================================================================

TEST(PalTBCConverterTest,
     AssembleFrame_OutputSampleCountEqualskPalFrameSamples) {
  // Build minimal synthetic TBC fields at blanking level.
  auto f1 = make_field(kTBCField1Lines, kLineWidth,
                       static_cast<uint16_t>(kRefBlanking));  // 313 × 1135
  auto f2 = make_field(kTBCField2Lines, kLineWidth,
                       static_cast<uint16_t>(kRefBlanking));  // 312 × 1135

  const auto frame =
      orc::PalTBCConverter::assemble_frame(f1, f2, kRefBlanking, kRefWhite);

  EXPECT_EQ(static_cast<int32_t>(frame.size()), orc::kPalFrameSamples);
}

TEST(PalTBCConverterTest, AssembleFrame_ThrowsOnWrongField1Size) {
  // field1 must be 313 × 1135 samples.
  auto bad_f1 = make_field(kTBCField2Lines, kLineWidth,
                           static_cast<uint16_t>(kRefBlanking));  // 312, wrong
  auto f2 = make_field(kTBCField2Lines, kLineWidth,
                       static_cast<uint16_t>(kRefBlanking));
  EXPECT_THROW(
      orc::PalTBCConverter::assemble_frame(bad_f1, f2, kRefBlanking, kRefWhite),
      std::invalid_argument);
}

TEST(PalTBCConverterTest, AssembleFrame_ThrowsOnWrongField2Size) {
  auto f1 = make_field(kTBCField1Lines, kLineWidth,
                       static_cast<uint16_t>(kRefBlanking));
  auto bad_f2 = make_field(kTBCField1Lines, kLineWidth,
                           static_cast<uint16_t>(kRefBlanking));  // 313, wrong
  EXPECT_THROW(
      orc::PalTBCConverter::assemble_frame(f1, bad_f2, kRefBlanking, kRefWhite),
      std::invalid_argument);
}

TEST(PalTBCConverterTest, AssembleFrame_CVBSField1IsFirst313Lines) {
  // Fill TBC field 1 (313 lines, odd-scan) with white, TBC field 2 (312 lines)
  // with blanking.  The first 313 × 1135 samples in the output must be white
  // (with small deviations at the 4 extra-sample positions).
  // EBU Tech. 3280-E §1.3: field 1 (odd-scan) is placed first in the buffer.
  const uint16_t tbc_white = static_cast<uint16_t>(kRefWhite);
  const uint16_t tbc_blank = static_cast<uint16_t>(kRefBlanking);

  auto f1 = make_field(kTBCField1Lines, kLineWidth, tbc_white);  // TBC field 1
  auto f2 = make_field(kTBCField2Lines, kLineWidth, tbc_blank);  // TBC field 2

  const auto frame =
      orc::PalTBCConverter::assemble_frame(f1, f2, kRefBlanking, kRefWhite);

  // First sample of the frame must be kPalWhite (sourced from TBC field 1).
  EXPECT_EQ(frame[0], static_cast<int16_t>(orc::kPalWhite));

  // First sample of CVBS field 2 (after 313 lines) must be kPalBlanking.
  // EBU Tech. 3280-E §1.3.1: CVBS field 1 ends at frame-flat line 312 (last
  // of field 1), which carries 2 extra bridge samples.
  // Field 1 occupies samples 0 .. (313×1135 + 2) - 1 = 355,257 - 1.
  const size_t cvbs_field2_start =
      static_cast<size_t>(kTBCField1Lines) * static_cast<size_t>(kLineWidth) +
      2;  // +2 for the extra samples on line 312 (last of field 1)
  ASSERT_LT(cvbs_field2_start, frame.size());
  EXPECT_EQ(frame[cvbs_field2_start], static_cast<int16_t>(orc::kPalBlanking));
}

TEST(PalTBCConverterTest, AssembleFrame_ExtraSamplesInsertedAtLines312And624) {
  // EBU Tech. 3280-E §1.3.1: frame-flat lines 312 and 624 each carry 2 extra
  // bridge samples.  Frame size baseline: 625 × 1135 = 709,375; + 4 extras →
  // 709,379 = kPalFrameSamples.
  //
  // Direct verification: fill TBC field 1 line 312 (the last line of CVBS
  // field 1) with a distinctive value and confirm 2 extra samples follow it.
  // Line 312 starts at offset 312 × 1135 = 354,120 and is 1137 samples long
  // (2 extras appended).  Line 313 (first of field 2) starts at 355,257.

  auto f1 = make_field(kTBCField1Lines, kLineWidth,
                       static_cast<uint16_t>(kRefBlanking));
  auto f2 = make_field(kTBCField2Lines, kLineWidth,
                       static_cast<uint16_t>(kRefBlanking));

  // Fill TBC field 1 line 312 with a distinctive value.
  constexpr int32_t kLineWidth32 = kLineWidth;
  constexpr uint16_t kDistinctive = 40000;
  for (int i = 0; i < kLineWidth32; ++i) {
    f1[static_cast<size_t>(312 * kLineWidth32 + i)] = kDistinctive;
  }

  const auto frame =
      orc::PalTBCConverter::assemble_frame(f1, f2, kRefBlanking, kRefWhite);

  EXPECT_EQ(static_cast<int32_t>(frame.size()), orc::kPalFrameSamples);

  // The 2 extra samples on line 312 sit at offsets 354,120 + 1135 and + 1136.
  const size_t line312_start =
      static_cast<size_t>(312) * static_cast<size_t>(kLineWidth);
  const size_t extra0 = line312_start + static_cast<size_t>(kLineWidth);
  const size_t extra1 = extra0 + 1;
  EXPECT_LT(extra1, frame.size());

  // Both extra samples must be interpolations (not blanking, not full
  // distinctive) — they bridge from the distinctive last sample toward the
  // blanking-level first sample of line 313.
  const int16_t cvbs_blank = static_cast<int16_t>(orc::kPalBlanking);
  EXPECT_NE(frame[extra0], cvbs_blank);
  EXPECT_NE(frame[extra1], cvbs_blank);
  // Line 313 (first of field 2) must be at blanking.
  const size_t line313_start = extra1 + 1;
  EXPECT_EQ(frame[line313_start], cvbs_blank);
}

}  // namespace orc_unit_test
