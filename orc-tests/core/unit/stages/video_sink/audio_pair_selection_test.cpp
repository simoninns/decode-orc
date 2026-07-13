/*
 * File:        audio_pair_selection_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for the video sink audio_channel_pairs parameter
 *              parser (audio channel pair selection)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../../orc/plugins/stages/sinks/common/audio_pair_selection.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace orc_unit_test {

TEST(AudioPairSelectionTest, All_SelectsEveryPairInOrder) {
  std::string error;
  const auto selection = orc::parse_audio_pair_selection("all", 3, error);
  ASSERT_TRUE(selection.has_value());
  EXPECT_THAT(*selection, testing::ElementsAre(0U, 1U, 2U));
}

TEST(AudioPairSelectionTest, EmptyValue_IsTreatedAsAll) {
  std::string error;
  const auto selection = orc::parse_audio_pair_selection("  ", 2, error);
  ASSERT_TRUE(selection.has_value());
  EXPECT_THAT(*selection, testing::ElementsAre(0U, 1U));
}

TEST(AudioPairSelectionTest, All_FailsWhenInputHasNoPairs) {
  std::string error;
  const auto selection = orc::parse_audio_pair_selection("all", 0, error);
  EXPECT_FALSE(selection.has_value());
  EXPECT_EQ(error, "The input carries no audio channel pairs");
}

TEST(AudioPairSelectionTest, CommaSeparatedIndices_ParseInGivenOrder) {
  std::string error;
  const auto selection = orc::parse_audio_pair_selection("2, 0", 3, error);
  ASSERT_TRUE(selection.has_value());
  EXPECT_THAT(*selection, testing::ElementsAre(2U, 0U));
}

TEST(AudioPairSelectionTest, DuplicateIndices_AreCollapsed) {
  std::string error;
  const auto selection = orc::parse_audio_pair_selection("1,1,0", 2, error);
  ASSERT_TRUE(selection.has_value());
  EXPECT_THAT(*selection, testing::ElementsAre(1U, 0U));
}

TEST(AudioPairSelectionTest, OutOfRangeIndex_FailsWithDiagnostic) {
  std::string error;
  const auto selection = orc::parse_audio_pair_selection("0,5", 2, error);
  EXPECT_FALSE(selection.has_value());
  EXPECT_EQ(error,
            "audio_channel_pairs index 5 is out of range: the input carries "
            "2 channel pair(s)");
}

TEST(AudioPairSelectionTest, MalformedToken_FailsWithDiagnostic) {
  std::string error;
  EXPECT_FALSE(orc::parse_audio_pair_selection("0,x", 2, error).has_value());
  EXPECT_FALSE(orc::parse_audio_pair_selection("0,,1", 2, error).has_value());
  EXPECT_FALSE(orc::parse_audio_pair_selection("-1", 2, error).has_value());
  EXPECT_FALSE(orc::parse_audio_pair_selection("0;1", 2, error).has_value());
}

}  // namespace orc_unit_test
