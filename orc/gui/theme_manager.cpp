/*
 * File:        theme_manager.cpp
 * Module:      orc-gui
 * Purpose:     Centralized theme mode parsing and resolution
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "theme_manager.h"

#include <QPalette>
#include <QStyleHints>

ThemeManager::ThemeManager(const QString &modeArgument)
    : mode_(Mode::Auto)
{
    const QString normalized = modeArgument.trimmed().toLower();

    if (normalized == "auto" || normalized.isEmpty()) {
        mode_ = Mode::Auto;
        return;
    }

    if (normalized == "light") {
        mode_ = Mode::Light;
        return;
    }

    if (normalized == "dark") {
        mode_ = Mode::Dark;
        return;
    }

    mode_ = Mode::Auto;
    invalidMode_ = modeArgument;
}

bool ThemeManager::hadInvalidMode() const
{
    return !invalidMode_.isEmpty();
}

QString ThemeManager::invalidMode() const
{
    return invalidMode_;
}

ThemeManager::Mode ThemeManager::mode() const
{
    return mode_;
}

QString ThemeManager::modeName() const
{
    return modeToString(mode_);
}

bool ThemeManager::shouldTrackSystemChanges() const
{
    return mode_ == Mode::Auto;
}

ThemeManager::Resolution ThemeManager::resolve(const QApplication &app) const
{
    if (mode_ == Mode::Light) {
        return Resolution {
            mode_,
            Qt::ColorScheme::Light,
            false,
            false,
            QStringLiteral("cli override")
        };
    }

    if (mode_ == Mode::Dark) {
        return Resolution {
            mode_,
            Qt::ColorScheme::Dark,
            true,
            false,
            QStringLiteral("cli override")
        };
    }

    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;
    if (app.styleHints()) {
        scheme = app.styleHints()->colorScheme();
    }

    if (scheme == Qt::ColorScheme::Dark) {
        return Resolution {
            mode_,
            scheme,
            true,
            false,
            QStringLiteral("auto (style hints)")
        };
    }

    if (scheme == Qt::ColorScheme::Light) {
        return Resolution {
            mode_,
            scheme,
            false,
            false,
            QStringLiteral("auto (style hints)")
        };
    }

    const bool darkFromPalette = isPaletteDark(app.palette());
    return Resolution {
        mode_,
        darkFromPalette ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light,
        darkFromPalette,
        true,
        QStringLiteral("auto (palette fallback)")
    };
}

QString ThemeManager::modeToString(Mode mode)
{
    switch (mode) {
    case Mode::Auto:
        return QStringLiteral("auto");
    case Mode::Light:
        return QStringLiteral("light");
    case Mode::Dark:
        return QStringLiteral("dark");
    }

    return QStringLiteral("auto");
}

QString ThemeManager::colorSchemeToString(Qt::ColorScheme scheme)
{
    switch (scheme) {
    case Qt::ColorScheme::Dark:
        return QStringLiteral("dark");
    case Qt::ColorScheme::Light:
        return QStringLiteral("light");
    case Qt::ColorScheme::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

bool ThemeManager::isPaletteDark(const QPalette &palette)
{
    const QColor windowColor = palette.color(QPalette::Window);
    const QColor textColor = palette.color(QPalette::WindowText);
    return windowColor.lightness() < textColor.lightness();
}