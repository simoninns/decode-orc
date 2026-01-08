/*
 * File:        main.cpp
 * Module:      orc-gui
 * Purpose:     Application entry point
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "mainwindow.h"
#include "logging.h"
#include "version.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QPalette>
#include <QStyleHints>
#include <QStyle>
#include <QProcess>
#include <QSplashScreen>
#include <QPixmap>
#include <QTimer>
#include <QPainter>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace orc {

static std::shared_ptr<spdlog::logger> g_gui_logger;

std::shared_ptr<spdlog::logger> get_gui_logger() {
    if (!g_gui_logger) {
        // Ensure core logger is initialized first
        auto core_logger = get_logger();
        if (!core_logger) {
            return nullptr;
        }
        
        // Create GUI logger that shares the core logger's sinks
        auto sinks = core_logger->sinks();
        g_gui_logger = std::make_shared<spdlog::logger>("gui", sinks.begin(), sinks.end());
        
        // Register it with spdlog
        spdlog::register_logger(g_gui_logger);
        
        // Match the pattern from core logger
        g_gui_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        
        // Match log level with core logger
        g_gui_logger->set_level(core_logger->level());
    }
    return g_gui_logger;
}

void reset_gui_logger() {
    g_gui_logger.reset();
}

} // namespace orc

// Check if GNOME is using dark theme
bool isGNOMEDarkTheme()
{
    // First, try to use gsettings to check GNOME color scheme
    QProcess process;
    process.start("gsettings", QStringList() << "get" << "org.gnome.desktop.interface" << "color-scheme");
    process.waitForFinished(1000);
    
    if (process.exitCode() == 0) {
        QString output = process.readAllStandardOutput().trimmed();
        // Remove quotes if present
        output = output.remove('\'');
        if (output.contains("dark", Qt::CaseInsensitive)) {
            return true;
        }
        if (output.contains("light", Qt::CaseInsensitive)) {
            return false;
        }
    }
    
    // Fallback: try gtk-theme setting
    process.start("gsettings", QStringList() << "get" << "org.gnome.desktop.interface" << "gtk-theme");
    process.waitForFinished(1000);
    
    if (process.exitCode() == 0) {
        QString output = process.readAllStandardOutput().trimmed();
        output = output.remove('\'');
        if (output.contains("dark", Qt::CaseInsensitive)) {
            return true;
        }
    }
    
    return false;
}

// Apply dark or light palette to the application
void applySystemTheme(QApplication& app, bool isDark)
{
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
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
        darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
        darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(127, 127, 127));
        
        app.setPalette(darkPalette);
    } else {
        // Light theme - use default Qt palette
        app.setPalette(app.style()->standardPalette());
    }
}

// Qt message handler that bridges to spdlog
void qtMessageHandler(QtMsgType type, const QMessageLogContext& /*context*/, const QString& msg)
{
    switch (type) {
    case QtDebugMsg:
        ORC_LOG_DEBUG("[Qt] {}", msg.toStdString());
        break;
    case QtInfoMsg:
        ORC_LOG_INFO("[Qt] {}", msg.toStdString());
        break;
    case QtWarningMsg:
        ORC_LOG_WARN("[Qt] {}", msg.toStdString());
        break;
    case QtCriticalMsg:
        ORC_LOG_ERROR("[Qt] {}", msg.toStdString());
        break;
    case QtFatalMsg:
        ORC_LOG_CRITICAL("[Qt] {}", msg.toStdString());
        break;
    }
}

int main(int argc, char *argv[])
{
    // Enable high DPI scaling
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    
    QApplication app(argc, argv);
    
    app.setApplicationName("orc-gui");
    app.setApplicationVersion(ORC_VERSION);
    app.setOrganizationName("domesday86");
    app.setWindowIcon(QIcon(":/orc-gui/icon.png"));
    
    // Detect and apply GNOME theme
    bool isDark = isGNOMEDarkTheme();
    
    // Use Fusion style which works well with custom palettes
    app.setStyle("Fusion");
    applySystemTheme(app, isDark);
    
    // Command-line argument parsing
    QCommandLineParser parser;
    parser.setApplicationDescription("Orc GUI - *-decode Orchestration GUI");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Add log level option
    QCommandLineOption logLevelOption(
        "log-level",
        "Set logging verbosity (trace, debug, info, warn, error, critical, off)",
        "level",
        "info"
    );
    parser.addOption(logLevelOption);
    
    // Add log file option
    QCommandLineOption logFileOption(
        "log-file",
        "Write logs to specified file (in addition to console)",
        "filename"
    );
    parser.addOption(logFileOption);
    
    // Add project file argument
    parser.addPositionalArgument("project", "Project file to open (optional)");
    
    parser.process(app);
    
    // Initialize logging system
    QString logLevel = parser.value(logLevelOption);
    QString logFile = parser.value(logFileOption);
    
    // Reset GUI logger if it exists (so it will be recreated with new sinks)
    orc::reset_gui_logger();
    
    orc::init_logging(logLevel.toStdString(), "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v", logFile.toStdString());
    
    // Now create GUI logger (it will get the sinks including file sink if specified)
    auto gui_logger = orc::get_gui_logger();
    if (gui_logger) {
        std::string level_str = logLevel.toStdString();
        if (level_str == "trace") {
            gui_logger->set_level(spdlog::level::trace);
        } else if (level_str == "debug") {
            gui_logger->set_level(spdlog::level::debug);
        } else if (level_str == "info") {
            gui_logger->set_level(spdlog::level::info);
        } else if (level_str == "warn" || level_str == "warning") {
            gui_logger->set_level(spdlog::level::warn);
        } else if (level_str == "error") {
            gui_logger->set_level(spdlog::level::err);
        } else if (level_str == "critical") {
            gui_logger->set_level(spdlog::level::critical);
        } else if (level_str == "off") {
            gui_logger->set_level(spdlog::level::off);
        } else {
            gui_logger->set_level(spdlog::level::info);
        }
        
        // Flush on warnings and above to avoid I/O thrashing during debug logging
        gui_logger->flush_on(spdlog::level::warn);
    }
    
    // Install Qt message handler to bridge Qt messages to spdlog
    qInstallMessageHandler(qtMessageHandler);
    
    ORC_LOG_INFO("orc-gui {} starting", ORC_VERSION);
    ORC_LOG_DEBUG("GNOME theme detected: {}", isDark ? "dark" : "light");
    
    MainWindow window;
    
    window.show();
    ORC_LOG_DEBUG("Main window shown");
    
    // Open project if provided (after window is shown so viewport has correct dimensions)
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        ORC_LOG_INFO("Opening project from command line: {}", args.first().toStdString());
        window.openProject(args.first());
    }
    
    // Create and show splash screen after main window to ensure proper z-order
    QPixmap logoPixmap(":/orc-gui/decode-orc-logo-small.png");
    
    // Create a larger pixmap to hold logo + text below it
    QPixmap splashPixmap(logoPixmap.width(), logoPixmap.height() + 120);
    splashPixmap.fill(Qt::transparent);
    
    // Paint logo and text onto the splash pixmap
    QPainter painter(&splashPixmap);
    painter.drawPixmap(0, 0, logoPixmap);
    
    // Set up font for main text
    QFont titleFont = painter.font();
    titleFont.setPointSize(titleFont.pointSize() * 4);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(Qt::white);
    
    // Draw "Decode Orc" below the logo
    QRect titleRect(0, logoPixmap.height() + 5, splashPixmap.width(), 60);
    painter.drawText(titleRect, Qt::AlignCenter, "Decode Orc");
    
    // Set up font for copyright text (smaller, normal weight)
    QFont copyrightFont = painter.font();
    copyrightFont.setPointSize(titleFont.pointSize() / 4);
    copyrightFont.setBold(false);
    painter.setFont(copyrightFont);
    
    // Draw copyright below the title
    QRect copyrightRect(0, logoPixmap.height() + 65, splashPixmap.width(), 40);
    painter.drawText(copyrightRect, Qt::AlignCenter, "(c) 2026 Simon Inns");
    painter.end();
    
    QSplashScreen splash(splashPixmap, Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    splash.show();
    app.processEvents();
    
    ORC_LOG_DEBUG("Splash screen displayed");
    
    // Close splash screen after 3 seconds
    QTimer::singleShot(3000, [&splash]() {
        splash.close();
        ORC_LOG_DEBUG("Splash screen closed");
    });
    
    int result = app.exec();
    ORC_LOG_INFO("orc-gui exiting");
    return result;
}
