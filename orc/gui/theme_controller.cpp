/*
 * File:        theme_controller.cpp
 * Module:      orc-gui
 * Purpose:     Runtime application of theme modes and OS colour-scheme tracking
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "theme_controller.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <QString>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

#include "logging.h"
#include "plotwidget.h"

namespace {
// QSettings key persisting the user's chosen theme mode across runs.
constexpr auto kThemeModeSettingsKey = "theme/mode";
}  // namespace

ThemeController* ThemeController::s_instance = nullptr;

ThemeController::ThemeController(QApplication& app,
                                 const QString& initialModeArgument,
                                 QObject* parent)
    : QObject(parent), app_(app), mode_(ThemeManager::Mode::Auto) {
  ThemeManager manager(initialModeArgument);
  if (manager.hadInvalidMode()) {
    ORC_LOG_WARN("Unknown theme value '{}', falling back to auto",
                 manager.invalidMode().toStdString());
  }
  mode_ = manager.mode();

  s_instance = this;

  applyCurrentMode(QStringLiteral("startup"));
  updateSystemTracking();
}

ThemeController::~ThemeController() {
  if (s_instance == this) {
    s_instance = nullptr;
  }
}

ThemeController* ThemeController::instance() { return s_instance; }

ThemeManager::Mode ThemeController::mode() const { return mode_; }

void ThemeController::setMode(ThemeManager::Mode mode) {
  if (mode == mode_) {
    return;
  }

  mode_ = mode;

  QSettings settings;
  settings.setValue(kThemeModeSettingsKey, ThemeManager::modeToString(mode_));

  applyCurrentMode(QStringLiteral("user selection"));
  updateSystemTracking();

  emit modeChanged(mode_);
}

void ThemeController::applyCurrentMode(const QString& changeReason) {
  ThemeManager manager(ThemeManager::modeToString(mode_));
  const ThemeManager::Resolution resolution = manager.resolve(app_);
  applyResolution(resolution);

  ORC_LOG_INFO(
      "Theme mode '{}' resolved to '{}' via {} ({})",
      manager.modeName().toStdString(),
      ThemeManager::colorSchemeToString(resolution.scheme).toStdString(),
      resolution.source.toStdString(), changeReason.toStdString());
  if (resolution.usedPaletteFallback) {
    ORC_LOG_WARN(
        "Theme auto-detection fell back to palette heuristic because Qt "
        "reported unknown color scheme");
  }
}

void ThemeController::applyResolution(
    const ThemeManager::Resolution& resolution) {
  app_.setProperty("isDarkTheme", resolution.isDark);
  app_.setProperty("themeMode", ThemeManager::modeToString(resolution.mode));
  applyPalette(app_, resolution.isDark);

  const auto widgets = app_.allWidgets();
  for (QWidget* widget : widgets) {
    if (auto* plotWidget = qobject_cast<PlotWidget*>(widget)) {
      plotWidget->updateTheme();
    }
  }
}

void ThemeController::updateSystemTracking() {
  const bool shouldTrack = (mode_ == ThemeManager::Mode::Auto);

  if (!shouldTrack) {
    if (tracking_connection_) {
      QObject::disconnect(tracking_connection_);
      tracking_connection_ = {};
      ORC_LOG_DEBUG("Runtime OS theme tracking disabled (forced theme mode)");
    }
    return;
  }

  if (tracking_connection_ || !app_.styleHints()) {
    return;
  }

  tracking_connection_ = QObject::connect(
      app_.styleHints(), &QStyleHints::colorSchemeChanged, this,
      [this](Qt::ColorScheme newScheme) {
        applyCurrentMode(
            QStringLiteral("OS color scheme changed to '%1'")
                .arg(ThemeManager::colorSchemeToString(newScheme)));
      });
  ORC_LOG_DEBUG("Runtime OS theme tracking enabled (auto mode)");
}

// Apply dark or light palette to the application.
void ThemeController::applyPalette(QApplication& app, bool isDark) {
  if (isDark) {
    // Dark theme palette
    QPalette darkPalette;

    // Base colors
    QColor darkGray(53, 53, 53);
    QColor darkerGray(42, 42, 42);
    QColor darkestGray(25, 25, 25);
    QColor lightGray(200, 200, 200);
    QColor blue(42, 130, 218);

    darkPalette.setColor(QPalette::Window, darkGray);
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, darkestGray);
    darkPalette.setColor(QPalette::AlternateBase, darkGray);
    darkPalette.setColor(QPalette::ToolTipBase, darkGray);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, darkGray);
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, blue);
    darkPalette.setColor(QPalette::Highlight, blue);
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    // Disabled colors
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText,
                         QColor(127, 127, 127));
    darkPalette.setColor(QPalette::Disabled, QPalette::Text,
                         QColor(127, 127, 127));
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText,
                         QColor(127, 127, 127));
    darkPalette.setColor(QPalette::Disabled, QPalette::Highlight,
                         QColor(80, 80, 80));
    darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                         QColor(127, 127, 127));

    app.setPalette(darkPalette);
  } else {
    // Light theme: use Qt style palette
    QPalette lightPalette = app.style()->standardPalette();
    app.setPalette(lightPalette);
  }

  const QString disabled_menu_text_color =
      isDark ? "rgb(127, 127, 127)" : "palette(mid)";

  app.setStyleSheet(
      QString("QMenuBar { background-color: palette(window); color: "
              "palette(window-text); }"
              "QMenuBar::item:selected { background-color: palette(highlight); "
              "color: palette(highlighted-text); }"
              "QMenuBar::item:disabled { color: %1; }"
              "QMenu { background-color: palette(window); color: "
              "palette(window-text); }"
              "QMenu::item:selected { background-color: palette(highlight); "
              "color: palette(highlighted-text); }"
              "QMenu::item:disabled { color: %1; }"
              "QMessageBox { background-color: palette(window); color: "
              "palette(window-text); }"
              "QMessageBox QLabel { color: palette(window-text); }"
              "QMessageBox QPushButton { background-color: palette(button); "
              "color: palette(button-text); border: 1px solid palette(mid); "
              "padding: 4px 12px; border-radius: 3px; min-width: 60px; }"
              "QMessageBox QPushButton:hover { background-color: "
              "palette(highlight); color: palette(highlighted-text); }"
              "QMessageBox QPushButton:pressed { background-color: "
              "palette(dark); color: palette(button-text); }")
          .arg(disabled_menu_text_color));
}
