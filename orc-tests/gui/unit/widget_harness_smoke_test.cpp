/*
 * File:        widget_harness_smoke_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Offscreen smoke test baseline for widget-tier GUI tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>

#include "fieldpreviewwidget.h"

namespace gui_unit_test {

namespace {

QApplication& ensureApplication() {
  if (auto* existing_app =
          qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *existing_app;
  }

  static int argc = 3;
  static char app_name[] = "orc-gui-widget-test";
  static char platform_opt[] = "-platform";
  static char platform_val[] = "offscreen";
  static char* argv[] = {app_name, platform_opt, platform_val, nullptr};
  static QApplication* app = [] {
    auto* created_app = new QApplication(argc, argv);
    created_app->setQuitOnLastWindowClosed(false);
    return created_app;
  }();
  return *app;
}

}  // namespace

TEST(GUIWidgetHarnessSmokeTest, fieldPreviewWidgetCanShowAndCloseOffscreen) {
  (void)ensureApplication();

  FieldPreviewWidget widget;

  widget.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(widget.isVisible());

  widget.close();
  QCoreApplication::processEvents();

  EXPECT_FALSE(widget.isVisible());
}

}  // namespace gui_unit_test