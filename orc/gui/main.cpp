/*
 * File:        main.cpp
 * Module:      orc-gui
 * Purpose:     Application entry point
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include <orc/stage/error_types.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "crash_handler.h"
#include "logging.h"
#include "mainwindow.h"
#include "project_presenter.h"  // For initCoreLogging
#include "theme_controller.h"
#include "version.h"

namespace fs = std::filesystem;

namespace orc {

static std::shared_ptr<spdlog::logger> g_gui_logger;
/// Initialize GUI logging system
/// @param level Log level string
/// @param pattern Log pattern
/// @param log_file Optional log file path
void init_gui_logging(const std::string& level, const std::string& pattern,
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
      auto file_sink =
          std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
      file_sink->set_pattern(pattern);
      sinks.push_back(file_sink);
    } catch (const spdlog::spdlog_ex& ex) {
      // If file logging fails, just continue with console only
      std::cerr << "Failed to create log file: " << ex.what() << std::endl;
    }
  }

  // Create logger
  g_gui_logger =
      std::make_shared<spdlog::logger>("gui", sinks.begin(), sinks.end());
  g_gui_logger->set_pattern(pattern);

  // Set log level
  if (level == "trace") {
    g_gui_logger->set_level(spdlog::level::trace);
  } else if (level == "debug") {
    g_gui_logger->set_level(spdlog::level::debug);
  } else if (level == "warn" || level == "warning") {
    g_gui_logger->set_level(spdlog::level::warn);
  } else if (level == "error") {
    g_gui_logger->set_level(spdlog::level::err);
  } else if (level == "critical") {
    g_gui_logger->set_level(spdlog::level::critical);
  } else if (level == "off") {
    g_gui_logger->set_level(spdlog::level::off);
  } else {
    // Default to info (covers "info" and any unrecognised level)
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

void reset_gui_logger() { g_gui_logger.reset(); }

}  // namespace orc

// Qt message handler that bridges to spdlog
void qtMessageHandler(QtMsgType type, const QMessageLogContext& /*context*/,
                      const QString& msg) {
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

int main(int argc, char* argv[]) {
  try {
    // Enable high DPI scaling
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

    // Ensure resources from orc-gui-lib are registered when linked statically.
    Q_INIT_RESOURCE(orc_gui_resources);

    app.setApplicationName("orc-gui");
    app.setApplicationVersion(ORC_VERSION);
    app.setOrganizationName("domesday86");
    app.setDesktopFileName("io.github.simoninns.decode-orc");
    app.setWindowIcon(QIcon(":/orc-gui/icon.png"));

    // Command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription("Decode Orc GUI");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption logLevelOption(QStringList{"l", "log-level"},
                                      "Set log level (trace, debug, info, "
                                      "warn, error, critical, off) for both "
                                      "GUI and core",
                                      "level", "info");
    parser.addOption(logLevelOption);

    QCommandLineOption themeOption(
        "theme", "Set UI theme (auto, light, dark). Default: auto", "mode",
        "auto");
    parser.addOption(themeOption);

    // Single shared log file option
    QCommandLineOption sharedLogFileOption(
        QStringList{"f", "log-file"},
        "Write both GUI and core logs to the specified file", "file");
    parser.addOption(sharedLogFileOption);

    QCommandLineOption quickProjectOption(
        "quick", "Create a quick project from a TBC/TBCC/TBCY file",
        "filename");
    parser.addOption(quickProjectOption);

    QCommandLineOption safeCorePluginsOption(
        "safe-core-plugins",
        "Clear user plugin registry and ignore ORC_STAGE_PLUGIN_PATHS for this "
        "run (core plugins only)");
    parser.addOption(safeCorePluginsOption);

    parser.addPositionalArgument("project", "Project file to open (optional)");
    parser.process(app);

    if (parser.isSet(safeCorePluginsOption)) {
      // Ensure no external env-configured plugin search paths are used for this
      // run.
      qputenv("ORC_STAGE_PLUGIN_PATHS", "");

      const auto reset_result =
          orc::presenters::ProjectPresenter::clearPluginRegistryForSafeMode();
      if (!reset_result.success) {
        QMessageBox::critical(
            nullptr, "Safe Startup Failed",
            QString("Failed to clear plugin registry for safe startup:\n%1")
                .arg(QString::fromStdString(reset_result.error_message)));
        return 1;
      }
    }

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
    if (parser.isSet(safeCorePluginsOption)) {
      ORC_LOG_WARN(
          "Safe startup mode enabled: plugin registry cleared and "
          "ORC_STAGE_PLUGIN_PATHS ignored for this run");
    }

    // Resolve the initial theme mode: an explicit --theme flag wins for this
    // run, otherwise fall back to the mode last chosen in the GUI (Tools >
    // Themes), defaulting to auto. The ThemeController applies the theme,
    // tracks OS colour-scheme changes in auto mode, and lets the GUI override
    // the mode at runtime.
    QString initialThemeMode = parser.value(themeOption);
    if (!parser.isSet(themeOption)) {
      initialThemeMode = QSettings().value("theme/mode", "auto").toString();
    }
    ThemeController themeController(app, initialThemeMode);

    // Initialize crash handler
    orc::CrashHandlerConfig crash_config;
    crash_config.application_name = "orc-gui";
    crash_config.version = ORC_VERSION;

    QString crashDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (crashDir.isEmpty()) {
      crashDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    if (!crashDir.isEmpty()) {
      crashDir += "/decode-orc-crashes";
      crash_config.output_directory = crashDir.toStdString();
    } else {
      crash_config.output_directory = fs::current_path().string();
    }

    crash_config.enable_coredump = true;
    crash_config.auto_upload_info = true;
    crash_config.primary_log_file = sharedLogFile.toStdString();
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

    auto load_startup_file = [&](const QString& file_path, bool quick_mode) {
      const char* action =
          quick_mode ? "quick project creation" : "project open";

      try {
        if (quick_mode) {
          window.quickProject(file_path);
        } else {
          window.openProject(file_path);
        }
      } catch (const orc::UserDataError& e) {
        ORC_LOG_WARN("Startup {} failed for '{}': {}", action,
                     file_path.toStdString(), e.what());
        QMessageBox::warning(&window, "Startup Error",
                             QString("%1 failed for '%2':\n%3")
                                 .arg(action)
                                 .arg(file_path)
                                 .arg(e.what()));
      } catch (const std::exception& e) {
        ORC_LOG_ERROR("Unhandled startup {} exception for '{}': {}", action,
                      file_path.toStdString(), e.what());

        std::string bundle_path =
            orc::create_crash_bundle(std::string("Unhandled startup ") +
                                     action + " exception: " + e.what());

        QString message =
            QString("An unexpected error occurred while processing '%1':\n%2")
                .arg(file_path)
                .arg(e.what());
        if (!bundle_path.empty()) {
          message += QString("\n\nDiagnostic bundle created:\n%1")
                         .arg(QString::fromStdString(bundle_path));
        }

        QMessageBox::critical(&window, "Startup Error", message);
      } catch (...) {
        ORC_LOG_ERROR("Unknown startup {} exception for '{}'", action,
                      file_path.toStdString());

        std::string bundle_path = orc::create_crash_bundle(
            std::string("Unknown startup ") + action + " exception");

        QString message =
            QString("An unknown error occurred while processing '%1'.")
                .arg(file_path);
        if (!bundle_path.empty()) {
          message += QString("\n\nDiagnostic bundle created:\n%1")
                         .arg(QString::fromStdString(bundle_path));
        }

        QMessageBox::critical(&window, "Startup Error", message);
      }
    };

    if (parser.isSet(quickProjectOption)) {
      QString quickFile = parser.value(quickProjectOption);
      ORC_LOG_INFO("Loading quick project from command line: {}",
                   quickFile.toStdString());
      load_startup_file(quickFile, true);
    } else {
      const QStringList args = parser.positionalArguments();
      if (!args.isEmpty()) {
        QString filePath = args.first();
        // Auto-detect file type by extension
        if (filePath.endsWith(".tbc", Qt::CaseInsensitive) ||
            filePath.endsWith(".tbcc", Qt::CaseInsensitive) ||
            filePath.endsWith(".tbcy", Qt::CaseInsensitive)) {
          ORC_LOG_INFO("Creating quick project from TBC file: {}",
                       filePath.toStdString());
          load_startup_file(filePath, true);
        } else if (filePath.endsWith(".orcprj", Qt::CaseInsensitive)) {
          ORC_LOG_INFO("Opening project from command line: {}",
                       filePath.toStdString());
          load_startup_file(filePath, false);
        } else {
          // Unknown file type - try opening as project (old behavior)
          ORC_LOG_WARN(
              "Unknown file extension for '{}', attempting to open as project "
              "file",
              filePath.toStdString());
          load_startup_file(filePath, false);
        }
      }
    }

    // Splash screen
    QPixmap logoPixmap(":/orc-gui/decode-orc_logotype-1024x286.png");
    QPixmap logoPixmapScaled = logoPixmap.scaled(512, 142, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
    QPixmap splashPixmap(logoPixmapScaled.width(),
                         logoPixmapScaled.height() + 60);
    splashPixmap.fill(Qt::transparent);

    QPainter painter(&splashPixmap);
    painter.drawPixmap(0, 0, logoPixmapScaled);

    QFont copyrightFont = painter.font();
    copyrightFont.setPointSize(copyrightFont.pointSize());
    copyrightFont.setBold(false);
    painter.setFont(copyrightFont);
    painter.setPen(Qt::white);
    QRect copyrightRect(0, logoPixmapScaled.height() + 10, splashPixmap.width(),
                        40);
    painter.drawText(copyrightRect, Qt::AlignCenter, "(c) 2026 Simon Inns");
    painter.end();

    QSplashScreen splash(splashPixmap,
                         Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    splash.show();
    app.processEvents();
    const QRect windowFrame = window.frameGeometry();
    const QPoint centeredPos =
        windowFrame.center() - QPoint(splash.width() / 2, splash.height() / 2);
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
  } catch (const std::exception& e) {
    std::cerr << "\nFATAL ERROR: " << e.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "\nFATAL ERROR: Unknown exception\n";
    return 1;
  }
}
