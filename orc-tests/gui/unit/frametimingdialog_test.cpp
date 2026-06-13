/*
 * File:        frametimingdialog_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Tests for FrameTimingDialog helpers (gui-logic tier)
 *              and dialog construction smoke test (gui-widget tier)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

// ============================================================================
// Tier-1 tests: formatColourFrameIndex()
// No QApplication required — pure string logic on an inline function.
// Run under the "gui-logic" CTest label.
// ============================================================================

#include "frametimingdialog.h"

#include <gtest/gtest.h>

#include <QCoreApplication>

namespace gui_unit_test {

// ---- formatColourFrameIndex() -----------------------------------------------

TEST(FormatColourFrameIndexTest, NegativeOne_ReturnsUnknown) {
  using orc::presenters::VideoSystem;
  EXPECT_EQ(::formatColourFrameIndex(-1, VideoSystem::PAL).toStdString(),
            "Unknown");
  EXPECT_EQ(::formatColourFrameIndex(-1, VideoSystem::NTSC).toStdString(),
            "Unknown");
  EXPECT_EQ(::formatColourFrameIndex(-1, VideoSystem::Unknown).toStdString(),
            "Unknown");
}

TEST(FormatColourFrameIndexTest, PAL_ReturnsNumericString_1to4) {
  using orc::presenters::VideoSystem;
  // ITU-R BT.470-6 §3.5.2: PAL uses a four-field colour sequence (1-4)
  EXPECT_EQ(::formatColourFrameIndex(1, VideoSystem::PAL).toStdString(), "1");
  EXPECT_EQ(::formatColourFrameIndex(2, VideoSystem::PAL).toStdString(), "2");
  EXPECT_EQ(::formatColourFrameIndex(3, VideoSystem::PAL).toStdString(), "3");
  EXPECT_EQ(::formatColourFrameIndex(4, VideoSystem::PAL).toStdString(), "4");
}

TEST(FormatColourFrameIndexTest, NTSC_Returns_A_For_0) {
  using orc::presenters::VideoSystem;
  // SMPTE 170M-2004 §11.2: NTSC uses a two-field sequence (A=0, B=1)
  EXPECT_EQ(::formatColourFrameIndex(0, VideoSystem::NTSC).toStdString(), "A");
}

TEST(FormatColourFrameIndexTest, NTSC_Returns_B_For_1) {
  using orc::presenters::VideoSystem;
  EXPECT_EQ(::formatColourFrameIndex(1, VideoSystem::NTSC).toStdString(), "B");
}

TEST(FormatColourFrameIndexTest, PAL_M_Returns_A_For_0) {
  using orc::presenters::VideoSystem;
  // PAL_M follows the NTSC two-field sequence (ITU-R BT.1700-1 Annex 1 Part B)
  EXPECT_EQ(::formatColourFrameIndex(0, VideoSystem::PAL_M).toStdString(), "A");
}

TEST(FormatColourFrameIndexTest, PAL_M_Returns_B_For_1) {
  using orc::presenters::VideoSystem;
  EXPECT_EQ(::formatColourFrameIndex(1, VideoSystem::PAL_M).toStdString(), "B");
}

TEST(FormatColourFrameIndexTest, Unknown_System_ReturnsNumericString) {
  using orc::presenters::VideoSystem;
  EXPECT_EQ(::formatColourFrameIndex(0, VideoSystem::Unknown).toStdString(), "0");
  EXPECT_EQ(::formatColourFrameIndex(2, VideoSystem::Unknown).toStdString(), "2");
}

// ============================================================================
// Tier-3 tests: FrameTimingDialog construction (gui-widget, offscreen)
// Requires QApplication. Run under the "gui-widget" CTest label.
// ============================================================================

#include <QApplication>

namespace {

QApplication& ensureApplication() {
  if (auto* existing =
          qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *existing;
  }
  static int argc = 3;
  static char app_name[] = "orc-frametimingdialog-test";
  static char platform_opt[] = "-platform";
  static char platform_val[] = "offscreen";
  static char* argv[] = {app_name, platform_opt, platform_val, nullptr};
  static QApplication* app = [] {
    auto* a = new QApplication(argc, argv);
    a->setQuitOnLastWindowClosed(false);
    return a;
  }();
  return *app;
}

}  // namespace

TEST(FrameTimingDialogSmokeTest, Dialog_ConstructsWithoutCrash) {
  (void)ensureApplication();
  FrameTimingDialog dialog;
  EXPECT_FALSE(dialog.isVisible());
}

TEST(FrameTimingDialogSmokeTest, Dialog_CurrentFrameId_DefaultsToZero) {
  (void)ensureApplication();
  FrameTimingDialog dialog;
  EXPECT_EQ(dialog.currentFrameId(), 0u);
}

TEST(FrameTimingDialogSmokeTest, Dialog_ShowAndHide) {
  (void)ensureApplication();
  FrameTimingDialog dialog;
  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(dialog.isVisible());
  dialog.hide();
  QCoreApplication::processEvents();
  EXPECT_FALSE(dialog.isVisible());
}

}  // namespace gui_unit_test
