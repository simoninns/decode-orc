/*
 * File:        frametimingdialog_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Smoke tests for FrameTimingDialog construction (gui-widget tier)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "frametimingdialog.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>

namespace gui_unit_test {

// ============================================================================
// Tier-3 tests: FrameTimingDialog construction (gui-widget, offscreen)
// Requires QApplication. Run under the "gui-widget" CTest label.
// ============================================================================

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

TEST(FrameTimingDialogSmokeTest, Dialog_CurrentFieldIndex_DefaultsToZero) {
  (void)ensureApplication();
  FrameTimingDialog dialog;
  EXPECT_EQ(dialog.currentFieldIndex(), 0u);
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
