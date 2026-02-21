/*
 * File:        theme_manager.h
 * Module:      orc-gui
 * Purpose:     Centralized theme mode parsing and resolution
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <QApplication>
#include <QString>

class ThemeManager
{
public:
    enum class Mode {
        Auto,
        Light,
        Dark,
    };

    struct Resolution {
        Mode mode;
        Qt::ColorScheme scheme;
        bool isDark;
        bool usedPaletteFallback;
        QString source;
    };

    explicit ThemeManager(const QString &modeArgument);

    bool hadInvalidMode() const;
    QString invalidMode() const;

    Mode mode() const;
    QString modeName() const;
    bool shouldTrackSystemChanges() const;

    Resolution resolve(const QApplication &app) const;

    static QString modeToString(Mode mode);
    static QString colorSchemeToString(Qt::ColorScheme scheme);

private:
    static bool isPaletteDark(const QPalette &palette);

    Mode mode_;
    QString invalidMode_;
};