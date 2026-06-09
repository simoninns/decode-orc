/*
 * File:        theme_manager_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Unit tests for ThemeManager theme mode parsing and resolution
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QString>

// Include headers from their installed locations
#include "theme_manager.h"

namespace gui_unit_test {

// =============================================================================
// ThemeManager Constructor and Mode Parsing Tests
// =============================================================================

TEST(ThemeManagerTest, constructorAutoModeFromEmptyString) {
  // Empty string should default to Auto mode
  ThemeManager manager("");
  EXPECT_EQ(manager.mode(), ThemeManager::Mode::Auto);
  EXPECT_FALSE(manager.hadInvalidMode());
}

TEST(ThemeManagerTest, constructorAutoModeFromAutoString) {
  // "auto" string should set Auto mode
  ThemeManager manager("auto");
  EXPECT_EQ(manager.mode(), ThemeManager::Mode::Auto);
  EXPECT_FALSE(manager.hadInvalidMode());
}

TEST(ThemeManagerTest, constructorLightMode) {
  // "light" string should set Light mode
  ThemeManager manager("light");
  EXPECT_EQ(manager.mode(), ThemeManager::Mode::Light);
  EXPECT_FALSE(manager.hadInvalidMode());
}

TEST(ThemeManagerTest, constructorDarkMode) {
  // "dark" string should set Dark mode
  ThemeManager manager("dark");
  EXPECT_EQ(manager.mode(), ThemeManager::Mode::Dark);
  EXPECT_FALSE(manager.hadInvalidMode());
}

TEST(ThemeManagerTest, constructorLightModeCaseInsensitive) {
  // Case variations should be accepted for "light"
  ThemeManager manager1("LIGHT");
  EXPECT_EQ(manager1.mode(), ThemeManager::Mode::Light);
  EXPECT_FALSE(manager1.hadInvalidMode());

  ThemeManager manager2("Light");
  EXPECT_EQ(manager2.mode(), ThemeManager::Mode::Light);
  EXPECT_FALSE(manager2.hadInvalidMode());
}

TEST(ThemeManagerTest, constructorDarkModeCaseInsensitive) {
  // Case variations should be accepted for "dark"
  ThemeManager manager1("DARK");
  EXPECT_EQ(manager1.mode(), ThemeManager::Mode::Dark);
  EXPECT_FALSE(manager1.hadInvalidMode());

  ThemeManager manager2("Dark");
  EXPECT_EQ(manager2.mode(), ThemeManager::Mode::Dark);
  EXPECT_FALSE(manager2.hadInvalidMode());
}

TEST(ThemeManagerTest, constructorAutoModeCaseInsensitive) {
  // Case variations should be accepted for "auto"
  ThemeManager manager1("AUTO");
  EXPECT_EQ(manager1.mode(), ThemeManager::Mode::Auto);
  EXPECT_FALSE(manager1.hadInvalidMode());

  ThemeManager manager2("Auto");
  EXPECT_EQ(manager2.mode(), ThemeManager::Mode::Auto);
  EXPECT_FALSE(manager2.hadInvalidMode());
}

TEST(ThemeManagerTest, constructorInvalidModeRecordsError) {
  // Invalid mode string should be recorded
  ThemeManager manager("invalid_mode");
  EXPECT_EQ(manager.mode(), ThemeManager::Mode::Auto);  // Defaults to Auto
  EXPECT_TRUE(manager.hadInvalidMode());
  EXPECT_EQ(manager.invalidMode().toStdString(), "invalid_mode");
}

TEST(ThemeManagerTest, constructorInvalidModePreservesOriginal) {
  // Invalid mode should preserve the original string (including
  // leading/trailing spaces)
  ThemeManager manager("   Invalid   ");
  EXPECT_TRUE(manager.hadInvalidMode());
  // The invalidMode_ stores the untrimmed original string
  EXPECT_EQ(manager.invalidMode().toStdString(), "   Invalid   ");
}

// =============================================================================
// Mode and Name Queries Tests
// =============================================================================

TEST(ThemeManagerTest, modeNameReturnsCorrectNameForAuto) {
  ThemeManager manager("auto");
  EXPECT_EQ(manager.modeName().toStdString(), "auto");
}

TEST(ThemeManagerTest, modeNameReturnsCorrectNameForLight) {
  ThemeManager manager("light");
  EXPECT_EQ(manager.modeName().toStdString(), "light");
}

TEST(ThemeManagerTest, modeNameReturnsCorrectNameForDark) {
  ThemeManager manager("dark");
  EXPECT_EQ(manager.modeName().toStdString(), "dark");
}

TEST(ThemeManagerTest, shouldTrackSystemChangesTrueForAuto) {
  // Auto mode should track system changes
  ThemeManager manager("auto");
  EXPECT_TRUE(manager.shouldTrackSystemChanges());
}

TEST(ThemeManagerTest, shouldTrackSystemChangesFalseForLight) {
  // Light mode should not track system changes (explicitly set)
  ThemeManager manager("light");
  EXPECT_FALSE(manager.shouldTrackSystemChanges());
}

TEST(ThemeManagerTest, shouldTrackSystemChangesFalseForDark) {
  // Dark mode should not track system changes (explicitly set)
  ThemeManager manager("dark");
  EXPECT_FALSE(manager.shouldTrackSystemChanges());
}

// =============================================================================
// Static String Conversion Tests
// =============================================================================

TEST(ThemeManagerTest, modeToStringAuto) {
  QString result = ThemeManager::modeToString(ThemeManager::Mode::Auto);
  EXPECT_EQ(result.toStdString(), "auto");
}

TEST(ThemeManagerTest, modeToStringLight) {
  QString result = ThemeManager::modeToString(ThemeManager::Mode::Light);
  EXPECT_EQ(result.toStdString(), "light");
}

TEST(ThemeManagerTest, modeToStringDark) {
  QString result = ThemeManager::modeToString(ThemeManager::Mode::Dark);
  EXPECT_EQ(result.toStdString(), "dark");
}

TEST(ThemeManagerTest, colorSchemeToStringDark) {
  QString result = ThemeManager::colorSchemeToString(Qt::ColorScheme::Dark);
  EXPECT_EQ(result.toStdString(), "dark");
}

TEST(ThemeManagerTest, colorSchemeToStringLight) {
  QString result = ThemeManager::colorSchemeToString(Qt::ColorScheme::Light);
  EXPECT_EQ(result.toStdString(), "light");
}

TEST(ThemeManagerTest, colorSchemeToStringUnknown) {
  QString result = ThemeManager::colorSchemeToString(Qt::ColorScheme::Unknown);
  EXPECT_EQ(result.toStdString(), "unknown");
}

// =============================================================================
// Resolution Tests
// =============================================================================

TEST(ThemeManagerTest, resolveLightModeReturnsLight) {
  ThemeManager manager("light");

  // Create a minimal QApplication for testing
  char* argv[] = {const_cast<char*>("test")};
  int argc = 1;
  QApplication app(argc, argv);

  auto resolution = manager.resolve(app);

  EXPECT_EQ(resolution.mode, ThemeManager::Mode::Light);
  EXPECT_EQ(resolution.scheme, Qt::ColorScheme::Light);
  EXPECT_FALSE(resolution.isDark);
  EXPECT_FALSE(resolution.usedPaletteFallback);
  EXPECT_EQ(resolution.source.toStdString(), "cli override");
}

TEST(ThemeManagerTest, resolveDarkModeReturnsDark) {
  ThemeManager manager("dark");

  char* argv[] = {const_cast<char*>("test")};
  int argc = 1;
  QApplication app(argc, argv);

  auto resolution = manager.resolve(app);

  EXPECT_EQ(resolution.mode, ThemeManager::Mode::Dark);
  EXPECT_EQ(resolution.scheme, Qt::ColorScheme::Dark);
  EXPECT_TRUE(resolution.isDark);
  EXPECT_FALSE(resolution.usedPaletteFallback);
  EXPECT_EQ(resolution.source.toStdString(), "cli override");
}

TEST(ThemeManagerTest, resolveAutoModeUsesSystemScheme) {
  ThemeManager manager("auto");

  char* argv[] = {const_cast<char*>("test")};
  int argc = 1;
  QApplication app(argc, argv);

  auto resolution = manager.resolve(app);

  // Auto mode should resolve to something (either Light or Dark)
  EXPECT_EQ(resolution.mode, ThemeManager::Mode::Auto);
  EXPECT_TRUE(resolution.scheme == Qt::ColorScheme::Light ||
              resolution.scheme == Qt::ColorScheme::Dark ||
              resolution.scheme == Qt::ColorScheme::Unknown);

  // Should indicate what was used (style hints or palette fallback)
  EXPECT_TRUE(resolution.source.contains("style hints") ||
              resolution.source.contains("palette fallback"));
}

TEST(ThemeManagerTest, resolveLightModeNoFallback) {
  // Light mode should not use palette fallback (it's explicitly set)
  ThemeManager manager("light");

  char* argv[] = {const_cast<char*>("test")};
  int argc = 1;
  QApplication app(argc, argv);

  auto resolution = manager.resolve(app);
  EXPECT_FALSE(resolution.usedPaletteFallback);
}

TEST(ThemeManagerTest, resolveDarkModeNoFallback) {
  // Dark mode should not use palette fallback (it's explicitly set)
  ThemeManager manager("dark");

  char* argv[] = {const_cast<char*>("test")};
  int argc = 1;
  QApplication app(argc, argv);

  auto resolution = manager.resolve(app);
  EXPECT_FALSE(resolution.usedPaletteFallback);
}

// =============================================================================
// Resolution Structure Tests
// =============================================================================

TEST(ThemeManagerTest, resolutionIncludesAllFields) {
  ThemeManager manager("light");

  char* argv[] = {const_cast<char*>("test")};
  int argc = 1;
  QApplication app(argc, argv);

  auto resolution = manager.resolve(app);

  // Verify all fields are populated
  EXPECT_FALSE(resolution.source.isEmpty());

  // isDark and usedPaletteFallback should have sensible values
  EXPECT_TRUE(resolution.isDark == false);  // Explicitly set light mode
  EXPECT_TRUE(resolution.usedPaletteFallback == false);
}

TEST(ThemeManagerTest, resolutionLightIsDarkIsFalse) {
  ThemeManager manager("light");

  char* argv[] = {const_cast<char*>("test")};
  int argc = 1;
  QApplication app(argc, argv);

  auto resolution = manager.resolve(app);
  EXPECT_FALSE(resolution.isDark);
}

TEST(ThemeManagerTest, resolutionDarkIsDarkIsTrue) {
  ThemeManager manager("dark");

  char* argv[] = {const_cast<char*>("test")};
  int argc = 1;
  QApplication app(argc, argv);

  auto resolution = manager.resolve(app);
  EXPECT_TRUE(resolution.isDark);
}

}  // namespace gui_unit_test
