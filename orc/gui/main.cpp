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
#include "crash_handler.h"
#include "version.h"
#include "project_presenter.h"  // For initCoreLogging
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
#include <QMessageBox>
#include <QStandardPaths>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <sstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace orc {

static std::shared_ptr<spdlog::logger> g_gui_logger;
/// Initialize GUI logging system
/// @param level Log level string
/// @param pattern Log pattern
/// @param log_file Optional log file path
void init_gui_logging(const std::string& level,
                      const std::string& pattern,
                      const std::string& log_file) {
    // Reset existing logger
    g_gui_logger.reset();
    
    // Create sinks
    std::vector<spdlog::sink_ptr> sinks;
    
    // Console sink (with colors)
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern(pattern);
    sinks.push_back(console_sink);
    
    // Optional file sink
    if (!log_file.empty()) {
        try {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
            file_sink->set_pattern(pattern);
            sinks.push_back(file_sink);
        } catch (const spdlog::spdlog_ex& ex) {
            // If file logging fails, just continue with console only
            std::cerr << "Failed to create log file: " << ex.what() << std::endl;
        }
    }
    
    // Create logger
    g_gui_logger = std::make_shared<spdlog::logger>("gui", sinks.begin(), sinks.end());
    g_gui_logger->set_pattern(pattern);
    
    // Set log level
    if (level == "trace") {
        g_gui_logger->set_level(spdlog::level::trace);
    } else if (level == "debug") {
        g_gui_logger->set_level(spdlog::level::debug);
    } else if (level == "info") {
        g_gui_logger->set_level(spdlog::level::info);
    } else if (level == "warn" || level == "warning") {
        g_gui_logger->set_level(spdlog::level::warn);
    } else if (level == "error") {
        g_gui_logger->set_level(spdlog::level::err);
    } else if (level == "critical") {
        g_gui_logger->set_level(spdlog::level::critical);
    } else if (level == "off") {
        g_gui_logger->set_level(spdlog::level::off);
    } else {
        g_gui_logger->set_level(spdlog::level::info);
    }
    
    // Flush on warnings and above
    g_gui_logger->flush_on(spdlog::level::warn);
    
    // Register with spdlog
    spdlog::register_logger(g_gui_logger);
}


std::shared_ptr<spdlog::logger> get_gui_logger() {
    if (!g_gui_logger) {
        // Initialize with defaults if not already done
        init_gui_logging();
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

    // Apply system theme
    bool isDark = isGNOMEDarkTheme();
    applySystemTheme(app, isDark);

    // Command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription("Decode Orc GUI");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption logLevelOption(
        QStringList{ "l", "log-level" },
        "Set log level (trace, debug, info, warn, error, critical, off) for both GUI and core",
        "level",
        "info");
    parser.addOption(logLevelOption);

    // Single shared log file option
    QCommandLineOption sharedLogFileOption(
        QStringList{ "f", "log-file" },
        "Write both GUI and core logs to the specified file",
        "file");
    parser.addOption(sharedLogFileOption);


    QCommandLineOption quickProjectOption(
        "quick",
        "Create a quick project from a TBC/TBCC/TBCY file",
        "filename");
    parser.addOption(quickProjectOption);

    parser.addPositionalArgument("project", "Project file to open (optional)");
    parser.process(app);

    // Initialize GUI and core logging
    QString logLevel = parser.value(logLevelOption);
    QString sharedLogFile = parser.value(sharedLogFileOption);

    // Initialize GUI logging
    orc::init_gui_logging(logLevel.toStdString(),
                          "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v",
                          sharedLogFile.toStdString());

    // Initialize core logging (same file) through presenters layer
    orc::presenters::initCoreLogging(logLevel.toStdString(),
                                     "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v",
                                     sharedLogFile.toStdString());

    // Bridge Qt messages to spdlog
    qInstallMessageHandler(qtMessageHandler);

    ORC_LOG_INFO("orc-gui {} starting", ORC_VERSION);
    ORC_LOG_DEBUG("GNOME theme detected: {}", isDark ? "dark" : "light");

    // Initialize crash handler
    orc::CrashHandlerConfig crash_config;
    crash_config.application_name = "orc-gui";
    crash_config.version = ORC_VERSION;

    QString crashDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (crashDir.isEmpty()) {
        crashDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    if (!crashDir.isEmpty()) {
        crashDir += "/decode-orc-crashes";
        fs::create_directories(crashDir.toStdString());
        crash_config.output_directory = crashDir.toStdString();
    } else {
        crash_config.output_directory = fs::current_path().string();
    }

    crash_config.enable_coredump = true;
    crash_config.auto_upload_info = true;
    crash_config.custom_info_callback = []() -> std::string {
        std::ostringstream info;
        info << "Working directory: " << fs::current_path().string() << "\n";
        info << "Qt version: " << qVersion() << "\n";
        return info.str();
    };

    if (!orc::init_crash_handler(crash_config)) {
        ORC_LOG_WARN("Failed to initialize crash handler");
    } else {
        ORC_LOG_DEBUG("Crash handler initialized - bundles will be saved to: {}",
                      crash_config.output_directory);
    }

    MainWindow window;
    window.show();
    ORC_LOG_DEBUG("Main window shown");

    if (parser.isSet(quickProjectOption)) {
        QString quickFile = parser.value(quickProjectOption);
        ORC_LOG_INFO("Loading quick project from command line: {}", quickFile.toStdString());
        window.quickProject(quickFile);
    } else {
        const QStringList args = parser.positionalArguments();
        if (!args.isEmpty()) {
            ORC_LOG_INFO("Opening project from command line: {}", args.first().toStdString());
            window.openProject(args.first());
        }
    }

    // Splash screen
    QPixmap logoPixmap(":/orc-gui/decode-orc_logotype-1024x286.png");
    QPixmap splashPixmap(logoPixmap.width(), logoPixmap.height() + 60);
    splashPixmap.fill(Qt::transparent);

    QPainter painter(&splashPixmap);
    painter.drawPixmap(0, 0, logoPixmap);
    
    QFont copyrightFont = painter.font();
    copyrightFont.setPointSize(copyrightFont.pointSize() * 2);
    copyrightFont.setBold(false);
    painter.setFont(copyrightFont);
    painter.setPen(Qt::white);
    QRect copyrightRect(0, logoPixmap.height() + 10, splashPixmap.width(), 40);
    painter.drawText(copyrightRect, Qt::AlignCenter, "(c) 2026 Simon Inns");
    painter.end();

    QSplashScreen splash(splashPixmap, Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    splash.show();
    app.processEvents();
    const QRect windowFrame = window.frameGeometry();
    const QPoint centeredPos = windowFrame.center() - QPoint(splash.width() / 2, splash.height() / 2);
    splash.move(centeredPos);
    app.processEvents();
    ORC_LOG_DEBUG("Splash screen displayed");
    QTimer::singleShot(3000, [&splash]() {
        splash.close();
        ORC_LOG_DEBUG("Splash screen closed");
    });

    int result = app.exec();
    ORC_LOG_INFO("orc-gui exiting");

    orc::cleanup_crash_handler();
    return result;
}
