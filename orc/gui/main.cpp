/*
 * File:        main.cpp
 * Module:      orc-gui
 * Purpose:     Application entry point
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "mainwindow.h"
#include "logging.h"
#include "version.h"
#include <QApplication>
#include <QCommandLineParser>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace orc {

static std::shared_ptr<spdlog::logger> g_gui_logger;

std::shared_ptr<spdlog::logger> get_gui_logger() {
    if (!g_gui_logger) {
        // Get the core logger's sink to share it
        auto core_logger = get_logger();
        if (!core_logger) {
            return nullptr;
        }
        
        // Create GUI logger that shares the core logger's sink
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

} // namespace orc

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
    QApplication app(argc, argv);
    
    app.setApplicationName("orc-gui");
    app.setApplicationVersion(ORC_VERSION);
    app.setOrganizationName("domesday86");
    
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
    
    // Add project file argument
    parser.addPositionalArgument("project", "Project file to open (optional)");
    
    parser.process(app);
    
    // Initialize logging system
    QString logLevel = parser.value(logLevelOption);
    orc::init_logging(logLevel.toStdString());
    
    // Ensure GUI logger is created and has the correct level
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
    }
    
    // Install Qt message handler to bridge Qt messages to spdlog
    qInstallMessageHandler(qtMessageHandler);
    
    ORC_LOG_INFO("orc-gui {} starting", ORC_VERSION);
    
    MainWindow window;
    
    // Open project if provided
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        ORC_LOG_INFO("Opening project from command line: {}", args.first().toStdString());
        window.openProject(args.first());
    }
    
    window.show();
    ORC_LOG_DEBUG("Main window shown, entering event loop");
    
    int result = app.exec();
    ORC_LOG_INFO("orc-gui exiting");
    return result;
}
