/*
 * File:        sourcefield_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for SourceField helper methods
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include "../../../../orc/core/stages/chroma_sink/decoders/sourcefield.h"

namespace orc_unit_test
{
    TEST(SourceFieldTest, getOffset_returnsTopForFirstFieldAndBottomForSecondField)
    {
        SourceField first_field;
        first_field.is_first_field = true;

        SourceField second_field;
        second_field.is_first_field = false;

        EXPECT_EQ(first_field.getOffset(), 0);
        EXPECT_EQ(second_field.getOffset(), 1);
    }

    TEST(SourceFieldTest, activeLineHelpers_mapFrameLinesToFieldLines)
    {
        orc::SourceParameters video_params;
        video_params.first_active_frame_line = 0;
        video_params.last_active_frame_line = 5;

        SourceField first_field;
        first_field.is_first_field = true;

        SourceField second_field;
        second_field.is_first_field = false;

        EXPECT_EQ(first_field.getFirstActiveLine(video_params), 0);
        EXPECT_EQ(first_field.getLastActiveLine(video_params), 3);

        EXPECT_EQ(second_field.getFirstActiveLine(video_params), 0);
        EXPECT_EQ(second_field.getLastActiveLine(video_params), 2);
    }
}
