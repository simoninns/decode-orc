// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    app.setApplicationName("orc-gui");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("ld-decode");
    
    // Command-line argument parsing
    QCommandLineParser parser;
    parser.setApplicationDescription("ORC GUI - LaserDisc Decode Orchestration GUI");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Add project file argument
    parser.addPositionalArgument("project", "Project file to open (optional)");
    
    parser.process(app);
    
    MainWindow window;
    
    // Open project if provided
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        window.openProject(args.first());
    }
    
    window.show();
    
    return app.exec();
}
