/*
 * File:        projectpropertiesdialog.h
 * Module:      orc-gui
 * Purpose:     Dialog for editing project properties
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef PROJECTPROPERTIESDIALOG_H
#define PROJECTPROPERTIESDIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QTextEdit;

/**
 * Dialog for editing project name and description
 */
class ProjectPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectPropertiesDialog(QWidget *parent = nullptr);
    ~ProjectPropertiesDialog();
    
    // Get/set project name
    QString projectName() const;
    void setProjectName(const QString& name);
    
    // Get/set project description
    QString projectDescription() const;
    void setProjectDescription(const QString& description);

private:
    void setupUI();
    
    QLineEdit* name_edit_;
    QTextEdit* description_edit_;
};

#endif // PROJECTPROPERTIESDIALOG_H
