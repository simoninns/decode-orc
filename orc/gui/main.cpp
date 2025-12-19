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
