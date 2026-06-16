/*
 * File:        dropout_util_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for dropout_util coordinate conversion functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <cvbs_signal_constants.h>
#include <dropout_util.h>
#include <gtest/gtest.h>

using namespace orc;
using namespace orc::dropout_util;

// ---------------------------------------------------------------------------
// NTSC — orthogonal, constant 910 samples/line
// ---------------------------------------------------------------------------

TEST(DropoutUtil_NTSC, FirstSampleMapsToField1Line0Sample0) {
  auto r = frame_sample_to_field_line(VideoSystem::NTSC, 0);
  EXPECT_EQ(r.field, 1);
  EXPECT_EQ(r.line, 0);
  EXPECT_EQ(r.sample, 0);
}

TEST(DropoutUtil_NTSC, LastSampleOfFirstLineIsCorrect) {
  // Last sample of line 0 (field 1) = sample 909
  auto r =
      frame_sample_to_field_line(VideoSystem::NTSC, kNtscSamplesPerLine - 1);
  EXPECT_EQ(r.field, 1);
  EXPECT_EQ(r.line, 0);
  EXPECT_EQ(r.sample, kNtscSamplesPerLine - 1);
}

TEST(DropoutUtil_NTSC, FirstSampleOfSecondLineIsCorrect) {
  auto r = frame_sample_to_field_line(VideoSystem::NTSC, kNtscSamplesPerLine);
  EXPECT_EQ(r.field, 1);
  EXPECT_EQ(r.line, 1);
  EXPECT_EQ(r.sample, 0);
}

TEST(DropoutUtil_NTSC, LastSampleOfField1IsCorrect) {
  // Field 1 ends at frame-flat line 261, last sample = kNtscField1Lines * 910 -
  // 1
  uint64_t last_field1_sample =
      static_cast<uint64_t>(kNtscField1Lines) * kNtscSamplesPerLine - 1;
  auto r = frame_sample_to_field_line(VideoSystem::NTSC, last_field1_sample);
  EXPECT_EQ(r.field, 1);
  EXPECT_EQ(r.line, kNtscField1Lines - 1);
  EXPECT_EQ(r.sample, kNtscSamplesPerLine - 1);
}

TEST(DropoutUtil_NTSC, FirstSampleOfField2IsCorrect) {
  uint64_t first_field2_sample =
      static_cast<uint64_t>(kNtscField1Lines) * kNtscSamplesPerLine;
  auto r = frame_sample_to_field_line(VideoSystem::NTSC, first_field2_sample);
  EXPECT_EQ(r.field, 2);
  EXPECT_EQ(r.line, 0);
  EXPECT_EQ(r.sample, 0);
}

TEST(DropoutUtil_NTSC, LastFrameSampleIsCorrect) {
  uint64_t last = kNtscFrameSamples - 1;
  auto r = frame_sample_to_field_line(VideoSystem::NTSC, last);
  EXPECT_EQ(r.field, 2);
  int32_t field2_lines = kNtscFrameLines - kNtscField1Lines;  // 263
  EXPECT_EQ(r.line, field2_lines - 1);
  EXPECT_EQ(r.sample, kNtscSamplesPerLine - 1);
}

TEST(DropoutUtil_NTSC, RoundTripField1) {
  uint64_t original = static_cast<uint64_t>(100) * kNtscSamplesPerLine + 55;
  auto fls = frame_sample_to_field_line(VideoSystem::NTSC, original);
  uint64_t reconstructed = field_line_to_frame_sample(
      VideoSystem::NTSC, fls.field, fls.line, fls.sample);
  EXPECT_EQ(reconstructed, original);
}

TEST(DropoutUtil_NTSC, RoundTripField2) {
  uint64_t original =
      static_cast<uint64_t>(kNtscField1Lines + 50) * kNtscSamplesPerLine + 200;
  auto fls = frame_sample_to_field_line(VideoSystem::NTSC, original);
  uint64_t reconstructed = field_line_to_frame_sample(
      VideoSystem::NTSC, fls.field, fls.line, fls.sample);
  EXPECT_EQ(reconstructed, original);
}

// ---------------------------------------------------------------------------
// PAL_M — orthogonal, constant 909 samples/line
// ---------------------------------------------------------------------------

TEST(DropoutUtil_PALM, FirstSampleMapsToField1Line0Sample0) {
  auto r = frame_sample_to_field_line(VideoSystem::PAL_M, 0);
  EXPECT_EQ(r.field, 1);
  EXPECT_EQ(r.line, 0);
  EXPECT_EQ(r.sample, 0);
}

TEST(DropoutUtil_PALM, LastFrameSampleIsCorrect) {
  uint64_t last = kPalMFrameSamples - 1;
  auto r = frame_sample_to_field_line(VideoSystem::PAL_M, last);
  EXPECT_EQ(r.field, 2);
  int32_t field2_lines = kPalMFrameLines - kPalMField1Lines;  // 263
  EXPECT_EQ(r.line, field2_lines - 1);
  EXPECT_EQ(r.sample, kPalMSamplesPerLine - 1);
}

TEST(DropoutUtil_PALM, RoundTrip) {
  uint64_t original =
      static_cast<uint64_t>(kPalMField1Lines + 30) * kPalMSamplesPerLine + 444;
  auto fls = frame_sample_to_field_line(VideoSystem::PAL_M, original);
  uint64_t reconstructed = field_line_to_frame_sample(
      VideoSystem::PAL_M, fls.field, fls.line, fls.sample);
  EXPECT_EQ(reconstructed, original);
}

// ---------------------------------------------------------------------------
// PAL — non-orthogonal, 2 lines with 1137 samples (EBU3280 normative)
// ---------------------------------------------------------------------------

TEST(DropoutUtil_PAL, FirstSampleMapsToField1Line0Sample0) {
  auto r = frame_sample_to_field_line(VideoSystem::PAL, 0);
  EXPECT_EQ(r.field, 1);
  EXPECT_EQ(r.line, 0);
  EXPECT_EQ(r.sample, 0);
}

TEST(DropoutUtil_PAL, LastSampleOfFirstLineIsCorrect) {
  // Line 0 is normal (1135 samples); last sample at offset 1134.
  auto r = frame_sample_to_field_line(VideoSystem::PAL, 1134);
  EXPECT_EQ(r.field, 1);
  EXPECT_EQ(r.line, 0);
  EXPECT_EQ(r.sample, 1134);
}

TEST(DropoutUtil_PAL, FirstSampleOfSecondLineIsCorrect) {
  auto r = frame_sample_to_field_line(VideoSystem::PAL, 1135);
  EXPECT_EQ(r.field, 1);
  EXPECT_EQ(r.line, 1);
  EXPECT_EQ(r.sample, 0);
}

TEST(DropoutUtil_PAL, NonOrthogonalLine312HasTwoExtraSamples) {
  // EBU3280 normative: line 312 (last of field 1) carries 1137 samples.
  // It starts at: 312 × 1135 = 354,120 (no extra lines before it).
  // Its last two samples sit at offsets 354,120 + 1135 = 355,255 and 355,256.
  uint64_t line312_start = 312ULL * 1135;
  uint64_t line312_last = line312_start + 1136;  // 1137 samples, last = +1136

  auto r_first = frame_sample_to_field_line(VideoSystem::PAL, line312_start);
  EXPECT_EQ(r_first.field, 1);
  EXPECT_EQ(r_first.line, 312);
  EXPECT_EQ(r_first.sample, 0);

  auto r_last = frame_sample_to_field_line(VideoSystem::PAL, line312_last);
  EXPECT_EQ(r_last.field, 1);
  EXPECT_EQ(r_last.line, 312);
  EXPECT_EQ(r_last.sample, 1136);
}

TEST(DropoutUtil_PAL, SampleAfterNonOrthogonalLine312IsLine313) {
  // After line 312's last sample (offset 312×1135 + 1136 = 355,256), the next
  // sample belongs to line 313 (field 2, line 0), sample 0.
  uint64_t line313_start = 312ULL * 1135 + 1137;  // 2 extra samples on line 312
  auto r = frame_sample_to_field_line(VideoSystem::PAL, line313_start);
  EXPECT_EQ(r.field, 2);
  EXPECT_EQ(r.line, 0);
  EXPECT_EQ(r.sample, 0);
}

TEST(DropoutUtil_PAL, FirstSampleOfField2IsCorrect) {
  // EBU3280 normative: field 1 has 312 normal lines + 1 long line (line 312).
  // = 312 × 1135 + 1 × 1137 = 354,120 + 1,137 = 355,257
  uint64_t field1_samples =
      static_cast<uint64_t>(kPalField1Lines - 1) * 1135 + 1 * 1137;
  // = 312 × 1135 + 1137 = 354,120 + 1,137 = 355,257
  auto r = frame_sample_to_field_line(VideoSystem::PAL, field1_samples);
  EXPECT_EQ(r.field, 2);
  EXPECT_EQ(r.line, 0);
  EXPECT_EQ(r.sample, 0);
}

TEST(DropoutUtil_PAL, LastFrameSampleIsCorrect) {
  uint64_t last = kPalFrameSamples - 1;
  auto r = frame_sample_to_field_line(VideoSystem::PAL, last);
  EXPECT_EQ(r.field, 2);
  // Field 2 has 312 lines (kPalFrameLines - kPalField1Lines = 312).
  EXPECT_EQ(r.line, kPalFrameLines - kPalField1Lines - 1);
  EXPECT_EQ(r.sample,
            1136);  // Last line (624) is non-orthogonal (1137 samples)
}

TEST(DropoutUtil_PAL, RoundTripNormalLine) {
  // A normal line well before the first non-orthogonal position.
  uint64_t original = field_line_to_frame_sample(VideoSystem::PAL, /*field=*/1,
                                                 /*line=*/100, /*sample=*/500);
  auto fls = frame_sample_to_field_line(VideoSystem::PAL, original);
  EXPECT_EQ(fls.field, 1);
  EXPECT_EQ(fls.line, 100);
  EXPECT_EQ(fls.sample, 500);
}

TEST(DropoutUtil_PAL, RoundTripNonOrthogonalLine312) {
  // Line 312 in field 1 is non-orthogonal (1137 samples).
  uint64_t original = field_line_to_frame_sample(VideoSystem::PAL, /*field=*/1,
                                                 /*line=*/312, /*sample=*/1136);
  auto fls = frame_sample_to_field_line(VideoSystem::PAL, original);
  EXPECT_EQ(fls.field, 1);
  EXPECT_EQ(fls.line, 312);
  EXPECT_EQ(fls.sample, 1136);
}

TEST(DropoutUtil_PAL, RoundTripField2NonOrthogonalLine) {
  // Frame-flat line 624 = field 2, line 311 (0-indexed) = last field-2 line.
  // It is non-orthogonal (1137 samples) per EBU3280 normative placement.
  uint64_t original = field_line_to_frame_sample(VideoSystem::PAL, /*field=*/2,
                                                 /*line=*/311, /*sample=*/700);
  auto fls = frame_sample_to_field_line(VideoSystem::PAL, original);
  EXPECT_EQ(fls.field, 2);
  EXPECT_EQ(fls.line, 311);
  EXPECT_EQ(fls.sample, 700);
}

TEST(DropoutUtil_PAL, TotalFrameSampleCountConsistency) {
  // The last frame-flat offset (kPalFrameSamples - 1) should round-trip.
  uint64_t last = kPalFrameSamples - 1;
  auto fls = frame_sample_to_field_line(VideoSystem::PAL, last);
  uint64_t reconstructed = field_line_to_frame_sample(
      VideoSystem::PAL, fls.field, fls.line, fls.sample);
  EXPECT_EQ(reconstructed, last);
}
