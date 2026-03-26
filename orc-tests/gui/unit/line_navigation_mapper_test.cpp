/*
 * File:        line_navigation_mapper_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Unit tests for centralized line-scope navigation mapping helper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include "line_navigation_mapper.h"

namespace gui_unit_test {

TEST(LineNavigationMapperTest, computeTarget_validStepDown_mapsToNextFieldLine)
{
    int observed_next_image_y = -1;
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
        [&observed_next_image_y, &observed_next_height](int image_y, int image_height) {
            observed_next_image_y = image_y;
            observed_next_height = image_height;
            return orc::ImageToFieldMappingResult{true, 4, 120};
        });

    EXPECT_TRUE(result.is_valid);
    EXPECT_EQ(result.field_index, 4u);
    EXPECT_EQ(result.line_number, 120);
    EXPECT_EQ(observed_next_image_y, 242);
    EXPECT_EQ(observed_next_height, 525);
}

TEST(LineNavigationMapperTest, computeTarget_invalidWhenCurrentFieldToImageMappingFails)
{
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

TEST(LineNavigationMapperTest, computeTarget_invalidAtTopBoundary)
{
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

TEST(LineNavigationMapperTest, computeTarget_invalidWhenSteppedImageToFieldMappingFails)
{
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
        [](int, int) {
            return orc::ImageToFieldMappingResult{false, 0, 0};
        });

    EXPECT_FALSE(result.is_valid);
}

TEST(LineNavigationMapperTest, computeTarget_upFromBottomWorksWhenCurrentImageYIsOutOfRange)
{
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

} // namespace gui_unit_test