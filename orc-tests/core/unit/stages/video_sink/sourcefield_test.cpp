/*
 * File:        sourcefield_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for SourceField helper methods
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/sinks/common/decoders/sourcefield.h"

#include <gtest/gtest.h>

namespace orc_unit_test {

TEST(SourceFieldTest,
     GetOffset_ReturnsTopForFirstFieldAndBottomForSecondField) {
  SourceField first_field;
  first_field.is_first_field = true;

  SourceField second_field;
  second_field.is_first_field = false;

  EXPECT_EQ(first_field.getOffset(), 0);
  EXPECT_EQ(second_field.getOffset(), 1);
}

TEST(SourceFieldTest, GetLine_UsesStridArithmeticWithoutLinePtrs) {
  const int16_t samples[] = {1, 2, 3, 4, 5, 6, 7, 8};

  SourceField field;
  field.data = samples;
  field.samples_per_line = 4;
  field.line_count = 2;

  EXPECT_EQ(field.getLine(0), samples);
  EXPECT_EQ(field.getLine(1), samples + 4);
}

TEST(SourceFieldTest, GetLine_UsesLinePtrsWhenPopulated) {
  const int16_t samples[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

  SourceField field;
  field.data = samples;
  field.samples_per_line = 4;
  field.line_count = 2;
  field.line_ptrs = {samples + 0, samples + 5};  // Non-uniform offsets

  EXPECT_EQ(field.getLine(0), samples + 0);
  EXPECT_EQ(field.getLine(1), samples + 5);
}

TEST(SourceFieldTest, GetLumaAndChromaLine_UsesStrideWithoutLinePtrs) {
  const int16_t luma[] = {10, 11, 12, 13, 20, 21, 22, 23};
  const int16_t chroma[] = {30, 31, 32, 33, 40, 41, 42, 43};

  SourceField field;
  field.is_yc = true;
  field.luma_data = luma;
  field.chroma_data = chroma;
  field.samples_per_line = 4;
  field.line_count = 2;

  EXPECT_EQ(field.getLumaLine(0), luma);
  EXPECT_EQ(field.getLumaLine(1), luma + 4);
  EXPECT_EQ(field.getChromaLine(0), chroma);
  EXPECT_EQ(field.getChromaLine(1), chroma + 4);
}

TEST(SourceFieldTest, GetLumaAndChromaLine_UsesLinePtrsWhenPopulated) {
  const int16_t luma[] = {10, 11, 12, 13, 20, 21, 22, 23, 24};
  const int16_t chroma[] = {30, 31, 32, 33, 40, 41, 42, 43, 44};

  SourceField field;
  field.is_yc = true;
  field.luma_data = luma;
  field.chroma_data = chroma;
  field.samples_per_line = 4;
  field.line_count = 2;
  field.luma_line_ptrs = {luma + 0, luma + 5};  // Non-uniform offsets
  field.chroma_line_ptrs = {chroma + 0, chroma + 5};

  EXPECT_EQ(field.getLumaLine(0), luma + 0);
  EXPECT_EQ(field.getLumaLine(1), luma + 5);
  EXPECT_EQ(field.getChromaLine(0), chroma + 0);
  EXPECT_EQ(field.getChromaLine(1), chroma + 5);
}

}  // namespace orc_unit_test
