/*
 * File:        amplitude_conversion_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for amplitude_conversion.h utilities
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <amplitude_conversion.h>
#include <common_types.h>
#include <gtest/gtest.h>

#include <cmath>

using namespace orc;

// ---------------------------------------------------------------------------
// PAL signal level constants used throughout these tests
// ---------------------------------------------------------------------------
static constexpr int32_t kPalBlanking = 256;
static constexpr int32_t kPalWhite = 844;
static constexpr int32_t kNtscBlanking = 256;
static constexpr int32_t kNtscWhite = 820;

// ---------------------------------------------------------------------------
// samples10_to_mv
// ---------------------------------------------------------------------------

TEST(AmplitudeConversion, Samples10ToMv_PalBlanking_IsZero) {
  EXPECT_DOUBLE_EQ(
      samples10_to_mv(kPalBlanking, kPalBlanking, kPalWhite, VideoSystem::PAL),
      0.0);
}

TEST(AmplitudeConversion, Samples10ToMv_PalWhite_Is700mV) {
  EXPECT_NEAR(
      samples10_to_mv(kPalWhite, kPalBlanking, kPalWhite, VideoSystem::PAL),
      700.0, 0.01);
}

TEST(AmplitudeConversion, Samples10ToMv_NtscBlanking_IsZero) {
  EXPECT_DOUBLE_EQ(samples10_to_mv(kNtscBlanking, kNtscBlanking, kNtscWhite,
                                   VideoSystem::NTSC),
                   0.0);
}

TEST(AmplitudeConversion, Samples10ToMv_NtscWhite_IsApprox714mV) {
  // NTSC active-video span is 714.3 mV (100/140 × 1000 mV)
  EXPECT_NEAR(
      samples10_to_mv(kNtscWhite, kNtscBlanking, kNtscWhite, VideoSystem::NTSC),
      714.3, 0.5);
}

// ---------------------------------------------------------------------------
// samples10_to_ire
// ---------------------------------------------------------------------------

TEST(AmplitudeConversion, Samples10ToIre_PalBlanking_IsZeroIre) {
  EXPECT_DOUBLE_EQ(samples10_to_ire(kPalBlanking, kPalBlanking, kPalWhite),
                   0.0);
}

TEST(AmplitudeConversion, Samples10ToIre_PalWhite_Is100Ire) {
  EXPECT_NEAR(samples10_to_ire(kPalWhite, kPalBlanking, kPalWhite), 100.0,
              0.01);
}

// ---------------------------------------------------------------------------
// ire_to_samples10
// ---------------------------------------------------------------------------

TEST(AmplitudeConversion, IreToSamples10_ZeroIre_IsBlanking) {
  EXPECT_EQ(ire_to_samples10(0.0, kPalBlanking, kPalWhite), kPalBlanking);
}

TEST(AmplitudeConversion, IreToSamples10_100Ire_IsWhite) {
  EXPECT_EQ(ire_to_samples10(100.0, kPalBlanking, kPalWhite), kPalWhite);
}

// ---------------------------------------------------------------------------
// display_to_samples10 round-trip (all three units)
// ---------------------------------------------------------------------------

TEST(AmplitudeConversion, RoundTrip_IRE_PalWhite) {
  const double ire = samples10_to_ire(kPalWhite, kPalBlanking, kPalWhite);
  const int32_t back =
      display_to_samples10(ire, kPalBlanking, kPalWhite, VideoSystem::PAL,
                           AmplitudeDisplayUnit::IRE);
  EXPECT_EQ(back, kPalWhite);
}

TEST(AmplitudeConversion, RoundTrip_Millivolts_PalWhite) {
  const double mv =
      samples10_to_mv(kPalWhite, kPalBlanking, kPalWhite, VideoSystem::PAL);
  const int32_t back =
      display_to_samples10(mv, kPalBlanking, kPalWhite, VideoSystem::PAL,
                           AmplitudeDisplayUnit::Millivolts);
  EXPECT_EQ(back, kPalWhite);
}

TEST(AmplitudeConversion, RoundTrip_Samples10Bit_PalWhite) {
  const int32_t back = display_to_samples10(
      static_cast<double>(kPalWhite), kPalBlanking, kPalWhite, VideoSystem::PAL,
      AmplitudeDisplayUnit::Samples10Bit);
  EXPECT_EQ(back, kPalWhite);
}

TEST(AmplitudeConversion, RoundTrip_IRE_PalBlanking) {
  const double ire = samples10_to_ire(kPalBlanking, kPalBlanking, kPalWhite);
  const int32_t back =
      display_to_samples10(ire, kPalBlanking, kPalWhite, VideoSystem::PAL,
                           AmplitudeDisplayUnit::IRE);
  EXPECT_EQ(back, kPalBlanking);
}

// ---------------------------------------------------------------------------
// default_amplitude_unit
// ---------------------------------------------------------------------------

TEST(AmplitudeConversion, DefaultUnit_NTSC_IsIRE) {
  EXPECT_EQ(default_amplitude_unit(VideoSystem::NTSC),
            AmplitudeDisplayUnit::IRE);
}

TEST(AmplitudeConversion, DefaultUnit_PAL_IsMillivolts) {
  EXPECT_EQ(default_amplitude_unit(VideoSystem::PAL),
            AmplitudeDisplayUnit::Millivolts);
}

TEST(AmplitudeConversion, DefaultUnit_PALM_IsMillivolts) {
  EXPECT_EQ(default_amplitude_unit(VideoSystem::PAL_M),
            AmplitudeDisplayUnit::Millivolts);
}

// ---------------------------------------------------------------------------
// amplitude_major_tick / amplitude_minor_tick
// ---------------------------------------------------------------------------

TEST(AmplitudeConversion, MajorTick_IRE_Is20) {
  EXPECT_DOUBLE_EQ(amplitude_major_tick(AmplitudeDisplayUnit::IRE), 20.0);
}

TEST(AmplitudeConversion, MinorTick_IRE_Is5) {
  EXPECT_DOUBLE_EQ(amplitude_minor_tick(AmplitudeDisplayUnit::IRE), 5.0);
}

TEST(AmplitudeConversion, MajorTick_Millivolts_Is100) {
  EXPECT_DOUBLE_EQ(amplitude_major_tick(AmplitudeDisplayUnit::Millivolts),
                   100.0);
}

TEST(AmplitudeConversion, MinorTick_Millivolts_Is50) {
  EXPECT_DOUBLE_EQ(amplitude_minor_tick(AmplitudeDisplayUnit::Millivolts),
                   50.0);
}

TEST(AmplitudeConversion, MajorTick_Samples10Bit_Is128) {
  EXPECT_DOUBLE_EQ(amplitude_major_tick(AmplitudeDisplayUnit::Samples10Bit),
                   128.0);
}

TEST(AmplitudeConversion, MinorTick_Samples10Bit_Is128) {
  EXPECT_DOUBLE_EQ(amplitude_minor_tick(AmplitudeDisplayUnit::Samples10Bit),
                   128.0);
}

// ---------------------------------------------------------------------------
// snap_ceil
// ---------------------------------------------------------------------------

TEST(AmplitudeConversion, SnapCeil_FractionalIRE_SnapsUpToNextMajor) {
  // 14.3 IRE should snap up to 20 (next multiple of 20)
  EXPECT_DOUBLE_EQ(
      snap_ceil(14.3, amplitude_major_tick(AmplitudeDisplayUnit::IRE)), 20.0);
}

TEST(AmplitudeConversion, SnapCeil_ExactMultiple_IsUnchanged) {
  EXPECT_DOUBLE_EQ(
      snap_ceil(20.0, amplitude_major_tick(AmplitudeDisplayUnit::IRE)), 20.0);
}

TEST(AmplitudeConversion, SnapCeil_FractionalMv_SnapsUpToNextMajor) {
  // 150.7 mV should snap up to 200 (next multiple of 100)
  EXPECT_DOUBLE_EQ(
      snap_ceil(150.7, amplitude_major_tick(AmplitudeDisplayUnit::Millivolts)),
      200.0);
}

TEST(AmplitudeConversion, SnapCeil_Exact100mV_IsUnchanged) {
  EXPECT_DOUBLE_EQ(
      snap_ceil(100.0, amplitude_major_tick(AmplitudeDisplayUnit::Millivolts)),
      100.0);
}

TEST(AmplitudeConversion, SnapCeil_Fractional10Bit_SnapsUpToNextMajor) {
  // 300 (10-bit) should snap up to 384 (next multiple of 128)
  EXPECT_DOUBLE_EQ(snap_ceil(300.0, amplitude_major_tick(
                                        AmplitudeDisplayUnit::Samples10Bit)),
                   384.0);
}

TEST(AmplitudeConversion, SnapCeil_ProducesNoFractionalTickPositions) {
  const double step = amplitude_major_tick(AmplitudeDisplayUnit::IRE);
  // Iterate with an integer count; compute the test value from it.
  constexpr int kSteps = 44;
  for (int i = 0; i < kSteps; ++i) {
    const double v = -40.0 + static_cast<double>(i) * 3.7;
    const double snapped = snap_ceil(v, step);
    EXPECT_DOUBLE_EQ(std::fmod(snapped, step), 0.0)
        << "snap_ceil(" << v << ", " << step << ") = " << snapped;
  }
}
