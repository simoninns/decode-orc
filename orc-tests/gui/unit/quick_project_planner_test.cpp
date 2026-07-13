/*
 * File:        quick_project_planner_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Unit tests for the quick-project downstream graph planner
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "quick_project_planner.h"

#include <gtest/gtest.h>

namespace gui_unit_test {
namespace {

using orc::gui::plan_quick_project_downstream;

TEST(QuickProjectPlannerTest, NonLdDecode_SourceFeedsVideoSinkDirectly) {
  const auto plan = plan_quick_project_downstream(/*is_ld_decode=*/false,
                                                  /*has_efm_sidecar=*/false);
  EXPECT_TRUE(plan.video_transforms.empty());
}

TEST(QuickProjectPlannerTest, NonLdDecode_EfmSidecarIgnored) {
  // EFM decode is only meaningful for ld-decode LaserDisc sources.
  const auto plan = plan_quick_project_downstream(/*is_ld_decode=*/false,
                                                  /*has_efm_sidecar=*/true);
  EXPECT_TRUE(plan.video_transforms.empty());
}

TEST(QuickProjectPlannerTest, LdDecodeWithoutEfm_InsertsOnlyDropoutCorrect) {
  const auto plan = plan_quick_project_downstream(/*is_ld_decode=*/true,
                                                  /*has_efm_sidecar=*/false);
  ASSERT_EQ(plan.video_transforms.size(), 1u);
  EXPECT_EQ(plan.video_transforms[0], "dropout_correct");
}

TEST(QuickProjectPlannerTest, LdDecodeWithEfm_SplicesDecodeOnly) {
  const auto plan = plan_quick_project_downstream(/*is_ld_decode=*/true,
                                                  /*has_efm_sidecar=*/true);

  // Video chain: source -> dropout_correct -> efm_audio_decode -> video sink.
  // The decode inclusion is enough — no standalone EFM sink is added.
  ASSERT_EQ(plan.video_transforms.size(), 2u);
  EXPECT_EQ(plan.video_transforms[0], "dropout_correct");
  EXPECT_EQ(plan.video_transforms[1], "efm_audio_decode");
}

}  // namespace
}  // namespace gui_unit_test
