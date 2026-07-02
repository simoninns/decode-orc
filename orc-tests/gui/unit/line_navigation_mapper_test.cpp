/*
 * File:        line_navigation_mapper_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Unit tests for centralized line-scope navigation mapping helper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "line_navigation_mapper.h"

#include <gtest/gtest.h>

#include <vector>

namespace gui_unit_test {

TEST(LineNavigationMapperTest,
     ComputeTargetValidStepDown_SkipsSameFieldLineAndMapsToNext) {
  std::vector<int> observed_image_ys;
  int observed_next_height = -1;

  const auto result = orc::gui::computeLineNavigationTarget(
      {
          .direction = 1,
          .current_field = 3,
          .current_line = 120,
          .image_height = 525,
      },
      [](uint64_t field_index, int field_line, int image_height) {
        EXPECT_EQ(field_index, 3u);
        EXPECT_EQ(field_line, 120);
        EXPECT_EQ(image_height, 525);
        return orc::FieldToImageMappingResult{true, 241};
      },
      [&observed_image_ys, &observed_next_height](int image_y,
                                                  int image_height) {
        observed_image_ys.push_back(image_y);
        observed_next_height = image_height;
        // Interlaced frame: row 242 is the other field on the same scan
        // line (visually unchanged), so the mapper must step once more to
        // row 243 to reach the next field line.
        if (image_y == 242) {
          return orc::ImageToFieldMappingResult{true, 4, 120};
        }
        return orc::ImageToFieldMappingResult{true, 4, 121};
      });

  EXPECT_TRUE(result.is_valid);
  EXPECT_EQ(result.field_index, 4u);
  EXPECT_EQ(result.line_number, 121);
  EXPECT_EQ(observed_image_ys, (std::vector<int>{242, 243}));
  EXPECT_EQ(observed_next_height, 525);
}

TEST(LineNavigationMapperTest,
     ComputeTargetInvalidWhenCurrentFieldToImageMapping_Fails) {
  bool map_image_called = false;

  const auto result = orc::gui::computeLineNavigationTarget(
      {
          .direction = 1,
          .current_field = 0,
          .current_line = 10,
          .image_height = 625,
      },
      [](uint64_t, int, int) {
        return orc::FieldToImageMappingResult{false, 0};
      },
      [&map_image_called](int, int) {
        map_image_called = true;
        return orc::ImageToFieldMappingResult{true, 0, 0};
      });

  EXPECT_FALSE(result.is_valid);
  EXPECT_FALSE(map_image_called);
}

TEST(LineNavigationMapperTest, Compute_TargetInvalidAtTopBoundary) {
  bool map_image_called = false;

  const auto result = orc::gui::computeLineNavigationTarget(
      {
          .direction = -1,
          .current_field = 1,
          .current_line = 0,
          .image_height = 525,
      },
      [](uint64_t, int, int) {
        return orc::FieldToImageMappingResult{true, 0};
      },
      [&map_image_called](int, int) {
        map_image_called = true;
        return orc::ImageToFieldMappingResult{true, 0, 0};
      });

  EXPECT_FALSE(result.is_valid);
  EXPECT_FALSE(map_image_called);
}

TEST(LineNavigationMapperTest,
     ComputeTargetInvalidWhenSteppedImageToFieldMapping_Fails) {
  const auto result = orc::gui::computeLineNavigationTarget(
      {
          .direction = 1,
          .current_field = 2,
          .current_line = 200,
          .image_height = 525,
      },
      [](uint64_t, int, int) {
        return orc::FieldToImageMappingResult{true, 400};
      },
      [](int, int) { return orc::ImageToFieldMappingResult{false, 0, 0}; });

  EXPECT_FALSE(result.is_valid);
}

TEST(LineNavigationMapperTest,
     ComputeTargetUpFromBottom_WorksWhenCurrentImageYIsOutOfRange) {
  int observed_next_image_y = -1;

  const auto result = orc::gui::computeLineNavigationTarget(
      {
          .direction = -1,
          .current_field = 1,
          .current_line = 262,
          .image_height = 525,
      },
      [](uint64_t, int, int) {
        // Reproduces asymmetry edge case at frame bottom.
        return orc::FieldToImageMappingResult{true, 525};
      },
      [&observed_next_image_y](int image_y, int) {
        observed_next_image_y = image_y;
        return orc::ImageToFieldMappingResult{true, 1, 261};
      });

  EXPECT_EQ(observed_next_image_y, 523);
  EXPECT_TRUE(result.is_valid);
  EXPECT_EQ(result.field_index, 1u);
  EXPECT_EQ(result.line_number, 261);
}

}  // namespace gui_unit_test