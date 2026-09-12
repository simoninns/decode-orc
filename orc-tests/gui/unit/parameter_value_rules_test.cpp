/*
 * File:        parameter_value_rules_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Tier 1 tests for the cross-parameter value rules
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "parameter_value_rules.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace gui_unit_test {
namespace {

using orc::gui::crossParameterErrors;

std::map<std::string, orc::ParameterValue> videoParamsValues(
    int32_t active_video_start, int32_t active_video_end,
    int32_t first_active_frame_line, int32_t last_active_frame_line,
    int32_t black_level, int32_t white_level) {
  return {{"activeVideoStart", active_video_start},
          {"activeVideoEnd", active_video_end},
          {"firstActiveFrameLine", first_active_frame_line},
          {"lastActiveFrameLine", last_active_frame_line},
          {"blackLevel", black_level},
          {"whiteLevel", white_level}};
}

}  // namespace

TEST(ParameterValueRulesTest, ReportsNothing_WhenThePalStandardValuesAreUsed) {
  // EBU Tech. 3280-E §1.2 / §1.1.1 Table 1: the standard PAL active window
  // and levels must not be reported as inconsistent.
  EXPECT_TRUE(
      crossParameterErrors(videoParamsValues(157, 1105, 44, 620, 256, 844))
          .empty());
}

TEST(ParameterValueRulesTest, ReportsNothing_WhenEveryValueIsInherited) {
  EXPECT_TRUE(
      crossParameterErrors(videoParamsValues(-1, -1, -1, -1, -1, -1)).empty());
}

TEST(ParameterValueRulesTest,
     ReportsNothing_WhenOnlyOneEndOfAPairIsGivenAndTheOtherIsInherited) {
  // The inherited end is the source's, which this cannot see: a value that
  // looks inverted against the sentinel is not an error.
  EXPECT_TRUE(crossParameterErrors(videoParamsValues(900, -1, -1, 620, -1, 844))
                  .empty());
}

TEST(ParameterValueRulesTest,
     ReportsTheActiveWindow_WhenItsStartIsNotBeforeItsEnd) {
  const auto after_the_end =
      crossParameterErrors(videoParamsValues(1105, 157, 44, 620, 256, 844));
  ASSERT_EQ(after_the_end.size(), 1u);
  EXPECT_EQ(after_the_end.front(),
            "Active Video Start must be less than Active Video End.");

  // An empty window describes no picture either.
  const auto equal =
      crossParameterErrors(videoParamsValues(157, 157, 44, 620, 256, 844));
  ASSERT_EQ(equal.size(), 1u);
  EXPECT_EQ(equal.front(),
            "Active Video Start must be less than Active Video End.");
}

TEST(ParameterValueRulesTest,
     ReportsTheActiveLines_WhenTheFirstIsNotBeforeTheLast) {
  const auto errors =
      crossParameterErrors(videoParamsValues(157, 1105, 620, 44, 256, 844));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors.front(),
            "First Active Frame Line must be less than Last Active Frame "
            "Line.");
}

TEST(ParameterValueRulesTest, ReportsTheLevels_WhenBlackIsNotBelowWhite) {
  const auto errors =
      crossParameterErrors(videoParamsValues(157, 1105, 44, 620, 844, 256));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors.front(), "Black Level must be less than White Level.");
}

TEST(ParameterValueRulesTest, ReportsEveryBrokenPair_WhenSeveralAreWrong) {
  EXPECT_EQ(
      crossParameterErrors(videoParamsValues(1105, 157, 620, 44, 844, 256))
          .size(),
      3u);
}

TEST(ParameterValueRulesTest, ReportsNothing_WhenTheStageHasNoSuchParameters) {
  // Every other stage's parameters pass through untouched: a rule whose
  // parameters are absent is not applied.
  const std::map<std::string, orc::ParameterValue> other_stage = {
      {"input_path", std::string("/captures/disc.tbc")},
      {"threshold", static_cast<int32_t>(12)},
      {"enabled", true}};
  EXPECT_TRUE(crossParameterErrors(other_stage).empty());
}

TEST(ParameterValueRulesTest,
     ReportsNothing_WhenAParameterIsNotAWholeNumberAtAll) {
  // A value of the wrong type is the type check's business, not this one's;
  // it must not be read as a number that happens to compare badly.
  const std::map<std::string, orc::ParameterValue> wrong_types = {
      {"activeVideoStart", std::string("157")}, {"activeVideoEnd", 1105.0}};
  EXPECT_TRUE(crossParameterErrors(wrong_types).empty());
}

TEST(ParameterValueRulesTest, ReadsAPairCarriedAsUnsignedValues) {
  const std::map<std::string, orc::ParameterValue> unsigned_values = {
      {"activeVideoStart", static_cast<uint32_t>(1105)},
      {"activeVideoEnd", static_cast<uint32_t>(157)}};
  ASSERT_EQ(crossParameterErrors(unsigned_values).size(), 1u);
}

}  // namespace gui_unit_test
