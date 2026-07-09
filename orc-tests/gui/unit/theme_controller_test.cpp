/*
 * File:        theme_controller_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Unit tests for ThemeController runtime theme application
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "theme_controller.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QSignalSpy>
#include <QStandardPaths>

#include "theme_manager.h"

namespace gui_unit_test {
namespace {

// Redirects QSettings writes to an isolated temporary location so setMode()
// persistence does not touch the developer's real configuration.
struct IsolatedSettings {
  IsolatedSettings() { QStandardPaths::setTestModeEnabled(true); }
  ~IsolatedSettings() { QStandardPaths::setTestModeEnabled(false); }
};

// Constructs a QApplication for a single test. Only one may exist at a time, so
// every test owns its instance for the duration of the test body.
struct TestApp {
  int argc = 1;
  char arg0[5] = {'t', 'e', 's', 't', '\0'};
  char* argv[1] = {arg0};
  QApplication app{argc, argv};
};

}  // namespace

// =============================================================================
// Construction and application
// =============================================================================

TEST(ThemeControllerTest, Construct_DarkMode_AppliesDarkProperties) {
  TestApp harness;
  ThemeController controller(harness.app, "dark");

  EXPECT_EQ(controller.mode(), ThemeManager::Mode::Dark);
  EXPECT_TRUE(harness.app.property("isDarkTheme").toBool());
  EXPECT_EQ(harness.app.property("themeMode").toString().toStdString(), "dark");
}

TEST(ThemeControllerTest, Construct_LightMode_AppliesLightProperties) {
  TestApp harness;
  ThemeController controller(harness.app, "light");

  EXPECT_EQ(controller.mode(), ThemeManager::Mode::Light);
  EXPECT_FALSE(harness.app.property("isDarkTheme").toBool());
  EXPECT_EQ(harness.app.property("themeMode").toString().toStdString(),
            "light");
}

TEST(ThemeControllerTest, Construct_InvalidMode_FallsBackToAuto) {
  TestApp harness;
  ThemeController controller(harness.app, "chartreuse");

  EXPECT_EQ(controller.mode(), ThemeManager::Mode::Auto);
}

// =============================================================================
// instance() lifecycle
// =============================================================================

TEST(ThemeControllerTest, Instance_TracksActiveController) {
  TestApp harness;
  EXPECT_EQ(ThemeController::instance(), nullptr);
  {
    ThemeController controller(harness.app, "auto");
    EXPECT_EQ(ThemeController::instance(), &controller);
  }
  EXPECT_EQ(ThemeController::instance(), nullptr);
}

// =============================================================================
// setMode() runtime override
// =============================================================================

TEST(ThemeControllerTest, SetMode_ChangesModeAndReappliesTheme) {
  IsolatedSettings settings;
  TestApp harness;
  ThemeController controller(harness.app, "light");
  ASSERT_FALSE(harness.app.property("isDarkTheme").toBool());

  QSignalSpy spy(&controller, &ThemeController::modeChanged);
  controller.setMode(ThemeManager::Mode::Dark);

  EXPECT_EQ(controller.mode(), ThemeManager::Mode::Dark);
  EXPECT_TRUE(harness.app.property("isDarkTheme").toBool());
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.takeFirst().at(0).value<ThemeManager::Mode>(),
            ThemeManager::Mode::Dark);
}

TEST(ThemeControllerTest, SetMode_SameMode_IsNoOp) {
  IsolatedSettings settings;
  TestApp harness;
  ThemeController controller(harness.app, "dark");

  QSignalSpy spy(&controller, &ThemeController::modeChanged);
  controller.setMode(ThemeManager::Mode::Dark);

  EXPECT_EQ(spy.count(), 0);
}

}  // namespace gui_unit_test
