/*
 * File:        audio_track_selection_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for the video sink audio_tracks parameter parser
 *              and per-track declared sample rates
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/sinks/common/audio_track_selection.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace orc_unit_test {

TEST(AudioTrackSelectionTest, All_SelectsEveryTrackInOrder) {
  std::string error;
  const auto selection = orc::parse_audio_track_selection("all", 3, error);
  ASSERT_TRUE(selection.has_value());
  EXPECT_THAT(*selection, testing::ElementsAre(0U, 1U, 2U));
}

TEST(AudioTrackSelectionTest, EmptyValue_IsTreatedAsAll) {
  std::string error;
  const auto selection = orc::parse_audio_track_selection("  ", 2, error);
  ASSERT_TRUE(selection.has_value());
  EXPECT_THAT(*selection, testing::ElementsAre(0U, 1U));
}

TEST(AudioTrackSelectionTest, All_FailsWhenInputHasNoTracks) {
  std::string error;
  const auto selection = orc::parse_audio_track_selection("all", 0, error);
  EXPECT_FALSE(selection.has_value());
  EXPECT_EQ(error, "The input carries no audio tracks");
}

TEST(AudioTrackSelectionTest, CommaSeparatedIndices_ParseInGivenOrder) {
  std::string error;
  const auto selection = orc::parse_audio_track_selection("2, 0", 3, error);
  ASSERT_TRUE(selection.has_value());
  EXPECT_THAT(*selection, testing::ElementsAre(2U, 0U));
}

TEST(AudioTrackSelectionTest, DuplicateIndices_AreCollapsed) {
  std::string error;
  const auto selection = orc::parse_audio_track_selection("1,1,0", 2, error);
  ASSERT_TRUE(selection.has_value());
  EXPECT_THAT(*selection, testing::ElementsAre(1U, 0U));
}

TEST(AudioTrackSelectionTest, OutOfRangeIndex_FailsWithDiagnostic) {
  std::string error;
  const auto selection = orc::parse_audio_track_selection("0,5", 2, error);
  EXPECT_FALSE(selection.has_value());
  EXPECT_EQ(error,
            "audio_tracks index 5 is out of range: the input carries 2 "
            "track(s)");
}

TEST(AudioTrackSelectionTest, MalformedToken_FailsWithDiagnostic) {
  std::string error;
  EXPECT_FALSE(orc::parse_audio_track_selection("0,x", 2, error).has_value());
  EXPECT_FALSE(orc::parse_audio_track_selection("0,,1", 2, error).has_value());
  EXPECT_FALSE(orc::parse_audio_track_selection("-1", 2, error).has_value());
  EXPECT_FALSE(orc::parse_audio_track_selection("0;1", 2, error).has_value());
}

TEST(AudioTrackSelectionTest, DeclaredRate_LockedPalAndFreeRunningAre44100) {
  orc::AudioTrackDescriptor desc;
  desc.locked = true;
  desc.sample_rate = {44100, 1};
  EXPECT_EQ(orc::audio_track_declared_rate(desc), 44100);

  desc.locked = false;
  EXPECT_EQ(orc::audio_track_declared_rate(desc), 44100);
}

TEST(AudioTrackSelectionTest, DeclaredRate_LockedNtscRoundsTo44056) {
  // Nearest integer to the exact NTSC locked rate 44100000/1001 Hz
  // (≈ 44055.944), not the free-running 44100.
  orc::AudioTrackDescriptor desc;
  desc.locked = true;
  desc.sample_rate = {44100000, 1001};
  EXPECT_EQ(orc::audio_track_declared_rate(desc), 44056);
}

TEST(AudioTrackSelectionTest, DeclaredRate_ZeroRateFallsBackToStandard) {
  orc::AudioTrackDescriptor desc;
  desc.sample_rate = {0, 1};
  EXPECT_EQ(orc::audio_track_declared_rate(desc), 44100);
}

}  // namespace orc_unit_test
