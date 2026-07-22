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

TEST(SplitYcFieldsTest, RoutesLumaAndChromaToSeparateCompositeFields) {
  const int16_t luma[] = {10, 11, 12, 13};
  const int16_t chroma[] = {30, 31, 32, 33};

  SourceField field;
  field.seq_no = 7;
  field.is_first_field = false;
  field.is_yc = true;
  field.luma_data = luma;
  field.chroma_data = chroma;
  field.samples_per_line = 4;
  field.line_count = 1;

  std::vector<SourceField> lumaFields;
  std::vector<SourceField> chromaFields;
  split_yc_fields({field}, lumaFields, chromaFields);

  ASSERT_EQ(lumaFields.size(), 1u);
  ASSERT_EQ(chromaFields.size(), 1u);

  // Metadata is carried through; the channel becomes plain composite data.
  EXPECT_EQ(lumaFields[0].seq_no, 7);
  EXPECT_FALSE(lumaFields[0].is_first_field);
  EXPECT_FALSE(lumaFields[0].is_yc);
  EXPECT_EQ(lumaFields[0].data, luma);
  EXPECT_EQ(lumaFields[0].luma_data, nullptr);
  EXPECT_EQ(lumaFields[0].chroma_data, nullptr);

  EXPECT_FALSE(chromaFields[0].is_yc);
  EXPECT_EQ(chromaFields[0].data, chroma);
  EXPECT_EQ(chromaFields[0].luma_data, nullptr);
  EXPECT_EQ(chromaFields[0].chroma_data, nullptr);
}

TEST(SplitYcFieldsTest, MovesPalLinePtrsIntoCompositeLinePtrs) {
  const int16_t luma[] = {10, 11, 12, 13, 20};
  const int16_t chroma[] = {30, 31, 32, 33, 40};

  SourceField field;
  field.is_yc = true;
  field.luma_data = luma;
  field.chroma_data = chroma;
  field.samples_per_line = 4;
  field.line_count = 2;
  field.luma_line_ptrs = {luma + 0, luma + 4};
  field.chroma_line_ptrs = {chroma + 0, chroma + 4};

  std::vector<SourceField> lumaFields;
  std::vector<SourceField> chromaFields;
  split_yc_fields({field}, lumaFields, chromaFields);

  // The per-channel line pointers become the composite line_ptrs, and the
  // Y/C line-pointer vectors are cleared.
  EXPECT_EQ(lumaFields[0].line_ptrs,
            std::vector<const int16_t*>({luma + 0, luma + 4}));
  EXPECT_TRUE(lumaFields[0].luma_line_ptrs.empty());
  EXPECT_TRUE(lumaFields[0].chroma_line_ptrs.empty());

  EXPECT_EQ(chromaFields[0].line_ptrs,
            std::vector<const int16_t*>({chroma + 0, chroma + 4}));
  EXPECT_TRUE(chromaFields[0].luma_line_ptrs.empty());
  EXPECT_TRUE(chromaFields[0].chroma_line_ptrs.empty());
}

TEST(SplitYcFieldsTest, ClearsExistingOutputAndPreservesInputOrder) {
  const int16_t luma0[] = {1};
  const int16_t chroma0[] = {2};
  const int16_t luma1[] = {3};
  const int16_t chroma1[] = {4};

  auto make_field = [](const int16_t* l, const int16_t* c) {
    SourceField f;
    f.is_yc = true;
    f.luma_data = l;
    f.chroma_data = c;
    f.samples_per_line = 1;
    f.line_count = 1;
    return f;
  };

  std::vector<SourceField> inputFields = {make_field(luma0, chroma0),
                                          make_field(luma1, chroma1)};

  // Pre-populate the outputs to confirm they are cleared, not appended to.
  std::vector<SourceField> lumaFields(3);
  std::vector<SourceField> chromaFields(1);
  split_yc_fields(inputFields, lumaFields, chromaFields);

  ASSERT_EQ(lumaFields.size(), 2u);
  ASSERT_EQ(chromaFields.size(), 2u);
  EXPECT_EQ(lumaFields[0].data, luma0);
  EXPECT_EQ(lumaFields[1].data, luma1);
  EXPECT_EQ(chromaFields[0].data, chroma0);
  EXPECT_EQ(chromaFields[1].data, chroma1);
}

}  // namespace orc_unit_test
