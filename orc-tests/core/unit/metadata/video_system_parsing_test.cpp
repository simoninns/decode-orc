/*
 * File:        video_system_parsing_test.cpp
 * Module:      orc-core unit tests
 * Purpose:     Unit tests for video system name parsing with SQLite vs JSON constraints
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>
#include <tbc_metadata.h>

// Note: lddecodemetadata.h is internal/private and not exported via public headers.
// parseVideoSystemName() is declared in lddecodemetadata.h but cannot be included here
// without adding a public forward declaration or exposing internals.
// For this test suite, we focus on testing video_system_from_string() which is the
// public-facing function for SQLite metadata parsing.
// The JSON fallback parsing is tested indirectly through the JSON reader integration.

namespace orc {

// Tests for video_system_from_string() - used for SQLite metadata
// SQLite should only accept the canonical "PAL_M" (underscore) form
class VideoSystemFromStringTest : public ::testing::Test {
};

TEST_F(VideoSystemFromStringTest, AcceptsCanonicalPal) {
    EXPECT_EQ(video_system_from_string("PAL"), VideoSystem::PAL);
}

TEST_F(VideoSystemFromStringTest, AcceptsCanonicalNtsc) {
    EXPECT_EQ(video_system_from_string("NTSC"), VideoSystem::NTSC);
}

TEST_F(VideoSystemFromStringTest, AcceptsCanonicalPalM) {
    EXPECT_EQ(video_system_from_string("PAL_M"), VideoSystem::PAL_M);
}

TEST_F(VideoSystemFromStringTest, RejectsHyphenatedPalM) {
    // SQLite uses only the canonical underscore form
    // This enforces that SQLite metadata must use "PAL_M" not "PAL-M"
    EXPECT_EQ(video_system_from_string("PAL-M"), VideoSystem::Unknown);
}

TEST_F(VideoSystemFromStringTest, RejectsUnknownFormat) {
    EXPECT_EQ(video_system_from_string("SECAM"), VideoSystem::Unknown);
    EXPECT_EQ(video_system_from_string("PAL_L"), VideoSystem::Unknown);
    EXPECT_EQ(video_system_from_string(""), VideoSystem::Unknown);
    EXPECT_EQ(video_system_from_string("pal_m"), VideoSystem::Unknown);  // case-sensitive
}

TEST_F(VideoSystemFromStringTest, SqliteOnlyAcceptsExactCanonicalForm) {
    // This test enforces the strict requirement:
    // SQLite metadata must use exactly "PAL_M" (underscore, uppercase)
    // No variations like "PAL-M", "pal_m", "Pal_M" are accepted
    
    EXPECT_NE(video_system_from_string("PAL_M"), VideoSystem::Unknown);
    EXPECT_EQ(video_system_from_string("PAL-M"), VideoSystem::Unknown);
    EXPECT_EQ(video_system_from_string("pal_m"), VideoSystem::Unknown);
    EXPECT_EQ(video_system_from_string("Pal_M"), VideoSystem::Unknown);
}

}  // namespace orc

