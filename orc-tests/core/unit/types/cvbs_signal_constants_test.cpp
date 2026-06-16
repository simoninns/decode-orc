/*
 * File:        cvbs_signal_constants_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for CVBS_U10_4FSC signal constants
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <cvbs_signal_constants.h>
#include <gtest/gtest.h>

#include <cmath>

using namespace orc;

// ---------------------------------------------------------------------------
// PAL derived relationships (EBU Tech. 3280-E)
// ---------------------------------------------------------------------------

TEST(CVBSSignalConstants, PalFrameSamplesMatchesSpec) {
  // EBU Tech. 3280-E: kPalFrameSamples == kPalFrameLines * 1135 + 4
  EXPECT_EQ(kPalFrameSamples, kPalFrameLines * 1135 + 4);
}

TEST(CVBSSignalConstants, PalSampleRateIsFourTimesFsc) {
  // EBU Tech. 3280-E §1.1: sample rate = 4 × fsc
  EXPECT_DOUBLE_EQ(kPalSampleRate, 4.0 * kPalFsc);
}

TEST(CVBSSignalConstants, PalNominalSamplesPerLineRelationship) {
  // 17,734,475 / 625 / 25 ≈ 1135.0064 (samples per line per frame)
  double computed = kPalSampleRate / (kPalFrameLines * 25.0);
  EXPECT_NEAR(computed, kPalSamplesPerLine, 1e-3);
}

TEST(CVBSSignalConstants, PalExtraSampleLinesMathAddsUp) {
  // EBU Tech. 3280-E §1.3.1: 2 long lines × 1137 + 623 normal lines × 1135
  // = 2,274 + 707,105 = 709,379 = kPalFrameSamples
  int32_t long_lines = 2;  // lines 312 and 624, each with 2 extra samples
  int32_t normal_lines = kPalFrameLines - long_lines;
  int32_t computed = normal_lines * kPalSamplesPerLineNominal +
                     long_lines * kPalMaxSamplesPerLine;
  EXPECT_EQ(computed, kPalFrameSamples);
}

TEST(CVBSSignalConstants, PalExtraSampleLinesAreWithinFrameBounds) {
  for (int32_t line : kPalExtraSampleLines) {
    EXPECT_GE(line, 0) << "Extra sample line " << line << " is negative";
    EXPECT_LT(line, kPalFrameLines) << "Extra sample line " << line
                                    << " >= kPalFrameLines=" << kPalFrameLines;
  }
}

TEST(CVBSSignalConstants, PalExtraSampleLinesHaveTwoEntriesPerLongLine) {
  // EBU Tech. 3280-E §1.3.1: lines 312 and 624 each carry 2 extra samples.
  // The array stores one entry per extra sample, so each long line appears
  // twice.
  int32_t count_312 = 0;
  int32_t count_624 = 0;
  for (int32_t e : kPalExtraSampleLines) {
    if (e == 312) ++count_312;
    if (e == 624) ++count_624;
  }
  EXPECT_EQ(count_312, 2);
  EXPECT_EQ(count_624, 2);
}

TEST(CVBSSignalConstants, PalLevelOrdering) {
  EXPECT_LT(kPalSyncTip, kPalBlanking);
  EXPECT_LT(kPalBlanking, kPalBlack);
  EXPECT_LT(kPalBlack, kPalWhite);
  EXPECT_LT(kPalWhite, kPalPeak);
}

// ---------------------------------------------------------------------------
// NTSC derived relationships (SMPTE 244M-2003)
// ---------------------------------------------------------------------------

TEST(CVBSSignalConstants, NtscFrameSamplesMatchesSpec) {
  // SMPTE 244M-2003 §4.1: kNtscFrameSamples == kNtscFrameLines *
  // kNtscSamplesPerLine
  EXPECT_EQ(kNtscFrameSamples, kNtscFrameLines * kNtscSamplesPerLine);
}

TEST(CVBSSignalConstants, NtscSampleRateIsFourTimesFsc) {
  EXPECT_DOUBLE_EQ(kNtscSampleRate, 4.0 * kNtscFsc);
}

TEST(CVBSSignalConstants, NtscLevelOrdering) {
  EXPECT_LT(kNtscSyncTip, kNtscBlanking);
  EXPECT_LT(kNtscBlanking, kNtscBlack);
  EXPECT_LT(kNtscBlack, kNtscWhite);
  EXPECT_LT(kNtscWhite, kNtscPeak);
}

// ---------------------------------------------------------------------------
// PAL_M derived relationships (ITU-R BT.1700-1 Annex 1 Part B)
// ---------------------------------------------------------------------------

TEST(CVBSSignalConstants, PalMFrameSamplesMatchesSpec) {
  // kPalMFrameSamples == kPalMFrameLines * kPalMSamplesPerLine
  EXPECT_EQ(kPalMFrameSamples, kPalMFrameLines * kPalMSamplesPerLine);
}

TEST(CVBSSignalConstants, PalMSampleRateIsFourTimesFsc) {
  EXPECT_DOUBLE_EQ(kPalMSampleRate, 4.0 * kPalMFsc);
}
