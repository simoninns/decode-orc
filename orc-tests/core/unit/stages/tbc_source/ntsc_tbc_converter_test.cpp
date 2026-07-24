/*
 * File:        ntsc_tbc_converter_test.cpp
 * Module:      orc-tests/core/unit/stages/tbc_source
 * Purpose:     Unit tests for NtscTBCConverter and PalMTBCConverter level
 * mapping and frame assembly.
 *
 * Tests:
 *   NtscTBCConverter:
 *   - tbc_to_cvbs level mapping at blanking and white (SMPTE 244M-2003 §4.1)
 *   - assemble_frame sample count equals kNtscFrameSamples = 477,750
 *   - assemble_frame throws on wrong field sizes
 *   - TBC field 1 (263 lines, VFR top) appears first in output
 *
 *   PalMTBCConverter:
 *   - tbc_to_cvbs maps to NTSC signal levels (kNtscBlanking, kNtscWhite)
 *   - assemble_frame sample count equals kPalMFrameSamples = 477,225
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/tbc_source/ntsc_tbc_converter.h"

#include <gtest/gtest.h>
#include <orc/stage/cvbs_signal_constants.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include "../../../../orc/plugins/stages/tbc_source/pal_m_tbc_converter.h"

namespace orc_unit_test {

// ============================================================================
// Helpers
// ============================================================================

static std::vector<uint16_t> make_field_u16(int32_t lines, int32_t width,
                                            uint16_t fill) {
  return std::vector<uint16_t>(
      static_cast<size_t>(lines) * static_cast<size_t>(width), fill);
}

// Reference TBC levels matching actual ld-decode NTSC files (CVBS_U10 × 64).
// kNtscBlanking × 64 = 240 × 64 = 15360 (0 IRE blanking reference)
// kNtscWhite    × 64 = 800 × 64 = 51200 (100 IRE white)
// Note: ld-decode JSON stores black16bIre = kNtscBlack × 64 = 282 × 64 = 18048
// (the 7.5 IRE setup level), NOT the blanking.  tbc_metadata_json_reader
// derives the 0 IRE blanking from black16bIre before passing it here.
constexpr int32_t kRefBlanking = orc::kTbcNtscBlanking;  // 15360
constexpr int32_t kRefBlack = orc::kTbcNtscBlack;        // 18048 (7.5 IRE)
constexpr int32_t kRefWhite = orc::kTbcNtscWhite;        // 51200

// NTSC TBC field line counts (ld-decode convention).
// TBC field 1 (even file index, isFirstField=true) = odd-scan/first temporal,
//   263 real lines → VFR field 1 (top spatial).
// TBC field 2 (odd file index) = even-scan/second temporal,
//   262 real lines → VFR field 2 (bottom spatial).
constexpr int32_t kNtscF1Lines = orc::kNtscField1Lines;  // 263 (TBC f1/VFR top)
constexpr int32_t kNtscF2Lines =
    orc::kNtscFrameLines - orc::kNtscField1Lines;  // 262 (TBC f2/VFR bottom)
constexpr int32_t kNtscLineW = orc::kNtscSamplesPerLine;  // 910

// PAL_M TBC field line counts (identical convention to NTSC).
constexpr int32_t kPalMF1Lines = orc::kPalMField1Lines;  // 263 (TBC f1/VFR top)
constexpr int32_t kPalMF2Lines =
    orc::kPalMFrameLines - orc::kPalMField1Lines;  // 262 (TBC f2/VFR bottom)
constexpr int32_t kPalMLineW = orc::kPalMSamplesPerLine;  // 909

// ============================================================================
// NtscTBCConverter — level mapping
// ============================================================================

TEST(NtscTBCConverterTest, LevelMapping_BlankingInputYieldsCVBSBlanking) {
  // SMPTE 244M-2003: blanking → kNtscBlanking = 240.
  const int16_t result = orc::NtscTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(kRefBlanking), kRefBlanking, kRefWhite);
  EXPECT_EQ(result, static_cast<int16_t>(orc::kNtscBlanking));
}

TEST(NtscTBCConverterTest, LevelMapping_WhiteInputYieldsCVBSWhite) {
  // SMPTE 244M-2003: white level → kNtscWhite = 800.
  const int16_t result = orc::NtscTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(kRefWhite), kRefBlanking, kRefWhite);
  EXPECT_EQ(result, static_cast<int16_t>(orc::kNtscWhite));
}

TEST(NtscTBCConverterTest, LevelMapping_LinearInterpolationMidpoint) {
  const int32_t tbc_mid = (kRefBlanking + kRefWhite) / 2;
  const int16_t result = orc::NtscTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(tbc_mid), kRefBlanking, kRefWhite);
  const int32_t expected = (orc::kNtscBlanking + orc::kNtscWhite) / 2;
  EXPECT_NEAR(result, expected, 1);
}

TEST(NtscTBCConverterTest,
     LevelMapping_BelowBlankingProducesValueBelowCVBSBlanking) {
  const int16_t result = orc::NtscTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(kRefBlanking - 1000), kRefBlanking, kRefWhite);
  EXPECT_LT(result, static_cast<int16_t>(orc::kNtscBlanking));
}

TEST(NtscTBCConverterTest, LevelMapping_BlackInputYieldsCVBSBlack) {
  // Verifies that when the correct 0 IRE blanking is used as the reference,
  // the 7.5 IRE picture black level (kTbcNtscBlack = 18048 = kNtscBlack × 64)
  // maps to kNtscBlack (282) in CVBS.  This confirms that
  // tbc_metadata_json_reader must NOT pass black16bIre as the blanking
  // reference — it must derive the true 0 IRE blanking first. SMPTE 170M-2004
  // Table 1 / SMPTE 244M-2003 §4.2.1.
  const int16_t result = orc::NtscTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(kRefBlack), kRefBlanking, kRefWhite);
  EXPECT_NEAR(result, static_cast<int16_t>(orc::kNtscBlack), 1);
}

// ============================================================================
// NtscTBCConverter — frame assembly
// ============================================================================

TEST(NtscTBCConverterTest,
     AssembleFrame_OutputSampleCountEqualsKNtscFrameSamples) {
  auto f1 = make_field_u16(kNtscF1Lines, kNtscLineW,
                           static_cast<uint16_t>(kRefBlanking));
  auto f2 = make_field_u16(kNtscF2Lines, kNtscLineW,
                           static_cast<uint16_t>(kRefBlanking));

  const auto frame =
      orc::NtscTBCConverter::assemble_frame(f1, f2, kRefBlanking, kRefWhite);

  EXPECT_EQ(static_cast<int32_t>(frame.size()), orc::kNtscFrameSamples);
}

TEST(NtscTBCConverterTest, AssembleFrame_ThrowsOnWrongField1Size) {
  // Field 1 must be exactly 263 × 910 = 239,530 samples.
  auto bad_f1 =
      make_field_u16(kNtscF2Lines, kNtscLineW,
                     static_cast<uint16_t>(kRefBlanking));  // 262, wrong
  auto f2 = make_field_u16(kNtscF2Lines, kNtscLineW,
                           static_cast<uint16_t>(kRefBlanking));
  EXPECT_THROW(orc::NtscTBCConverter::assemble_frame(bad_f1, f2, kRefBlanking,
                                                     kRefWhite),
               std::invalid_argument);
}

TEST(NtscTBCConverterTest, AssembleFrame_ThrowsOnWrongField2Size) {
  auto f1 = make_field_u16(kNtscF1Lines, kNtscLineW,
                           static_cast<uint16_t>(kRefBlanking));
  auto bad_f2 =
      make_field_u16(kNtscF1Lines, kNtscLineW,
                     static_cast<uint16_t>(kRefBlanking));  // 262, wrong
  EXPECT_THROW(orc::NtscTBCConverter::assemble_frame(f1, bad_f2, kRefBlanking,
                                                     kRefWhite),
               std::invalid_argument);
}

TEST(NtscTBCConverterTest, AssembleFrame_VfrField1IsFirstFromTbcField1) {
  // TBC field 1 (odd-scan/first temporal, 263 lines) → VFR field 1 (top
  // spatial, first in output). TBC field 2 (even-scan, 262 lines) → VFR field 2
  // (bottom spatial, second in output).
  const uint16_t tbc_white = static_cast<uint16_t>(kRefWhite);
  const uint16_t tbc_blank = static_cast<uint16_t>(kRefBlanking);

  auto f1 = make_field_u16(kNtscF1Lines, kNtscLineW,
                           tbc_white);  // TBC f1 = 263 lines, white
  auto f2 = make_field_u16(kNtscF2Lines, kNtscLineW,
                           tbc_blank);  // TBC f2 = 262 lines, blank

  const auto frame =
      orc::NtscTBCConverter::assemble_frame(f1, f2, kRefBlanking, kRefWhite);

  // First sample in output is from TBC field 1 (white → VFR field 1, top).
  EXPECT_EQ(frame[0], static_cast<int16_t>(orc::kNtscWhite));

  // VFR field 2 (from TBC field 2 = blank) begins at offset 263 × 910 =
  // 239,530.
  const size_t vfr_field2_start =
      static_cast<size_t>(kNtscF1Lines) * static_cast<size_t>(kNtscLineW);
  ASSERT_LT(vfr_field2_start, frame.size());
  EXPECT_EQ(frame[vfr_field2_start], static_cast<int16_t>(orc::kNtscBlanking));
}

TEST(NtscTBCConverterTest, AssembleFrame_NoExtraSamplesInserted) {
  // NTSC is orthogonal: output size = exactly 477,750 with no padding or
  // extra samples.  Verified via: 263×910 + 262×910 = 477,750 =
  // kNtscFrameSamples.
  auto f1 = make_field_u16(kNtscF1Lines, kNtscLineW,
                           static_cast<uint16_t>(kRefBlanking));
  auto f2 = make_field_u16(kNtscF2Lines, kNtscLineW,
                           static_cast<uint16_t>(kRefBlanking));

  const auto frame =
      orc::NtscTBCConverter::assemble_frame(f1, f2, kRefBlanking, kRefWhite);

  EXPECT_EQ(
      frame.size(),
      static_cast<size_t>(kNtscF1Lines) * static_cast<size_t>(kNtscLineW) +
          static_cast<size_t>(kNtscF2Lines) * static_cast<size_t>(kNtscLineW));
}

// ============================================================================
// PalMTBCConverter — level mapping
// ============================================================================

TEST(PalMTBCConverterTest, LevelMapping_BlankingInputYieldsCVBSBlanking) {
  // ITU-R BT.1700-1 Annex 1 Part B: PAL_M uses NTSC levels (kNtscBlanking=240).
  const int16_t result = orc::PalMTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(kRefBlanking), kRefBlanking, kRefWhite);
  EXPECT_EQ(result, static_cast<int16_t>(orc::kNtscBlanking));
}

TEST(PalMTBCConverterTest, LevelMapping_WhiteInputYieldsCVBSWhite) {
  const int16_t result = orc::PalMTBCConverter::tbc_to_cvbs(
      static_cast<uint16_t>(kRefWhite), kRefBlanking, kRefWhite);
  EXPECT_EQ(result, static_cast<int16_t>(orc::kNtscWhite));
}

// ============================================================================
// PalMTBCConverter — frame assembly
// ============================================================================

TEST(PalMTBCConverterTest,
     AssembleFrame_OutputSampleCountEqualsKPalMFrameSamples) {
  auto f1 = make_field_u16(kPalMF1Lines, kPalMLineW,
                           static_cast<uint16_t>(kRefBlanking));
  auto f2 = make_field_u16(kPalMF2Lines, kPalMLineW,
                           static_cast<uint16_t>(kRefBlanking));

  const auto frame =
      orc::PalMTBCConverter::assemble_frame(f1, f2, kRefBlanking, kRefWhite);

  EXPECT_EQ(static_cast<int32_t>(frame.size()), orc::kPalMFrameSamples);
}

TEST(PalMTBCConverterTest, AssembleFrame_ThrowsOnWrongFieldSize) {
  auto bad_f1 = make_field_u16(kPalMF2Lines, kPalMLineW,
                               static_cast<uint16_t>(kRefBlanking));
  auto f2 = make_field_u16(kPalMF2Lines, kPalMLineW,
                           static_cast<uint16_t>(kRefBlanking));
  EXPECT_THROW(orc::PalMTBCConverter::assemble_frame(bad_f1, f2, kRefBlanking,
                                                     kRefWhite),
               std::invalid_argument);
}

TEST(PalMTBCConverterTest, AssembleFrame_OrthogonalNoExtraSamples) {
  // PAL_M: 262×909 + 263×909 = 477,225 = kPalMFrameSamples.
  auto f1 = make_field_u16(kPalMF1Lines, kPalMLineW,
                           static_cast<uint16_t>(kRefBlanking));
  auto f2 = make_field_u16(kPalMF2Lines, kPalMLineW,
                           static_cast<uint16_t>(kRefBlanking));

  const auto frame =
      orc::PalMTBCConverter::assemble_frame(f1, f2, kRefBlanking, kRefWhite);

  EXPECT_EQ(
      frame.size(),
      static_cast<size_t>(kPalMF1Lines) * static_cast<size_t>(kPalMLineW) +
          static_cast<size_t>(kPalMF2Lines) * static_cast<size_t>(kPalMLineW));
}

}  // namespace orc_unit_test
