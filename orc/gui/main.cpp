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
    
    // Add TBC source argument
    parser.addPositionalArgument("source", "TBC source file to load (optional)");
    
    parser.process(app);
    
    MainWindow window;
    
    // Load source if provided
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        window.loadSource(args.first());
    }
    
    window.show();
    
    return app.exec();
}
