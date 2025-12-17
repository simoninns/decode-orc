// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    app.setApplicationName("orc-gui");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("ld-decode");
    
    MainWindow window;
    window.show();
    
    return app.exec();
}
