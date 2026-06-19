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

#include <common_types.h>

#include <QComboBox>
#include <QDialog>
#include <QString>

class QLineEdit;
class QTextEdit;

/**
 * Dialog for editing project name and description
 */
class ProjectPropertiesDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ProjectPropertiesDialog(QWidget* parent = nullptr);
  ~ProjectPropertiesDialog();

  // Get/set project name
  QString projectName() const;
  void setProjectName(const QString& name);

  // Get/set project description
  QString projectDescription() const;
  void setProjectDescription(const QString& description);

  // Get/set amplitude display unit
  orc::AmplitudeDisplayUnit amplitudeUnit() const;
  void setAmplitudeUnit(orc::AmplitudeDisplayUnit unit);

 private:
  void setupUI();

  QLineEdit* name_edit_;
  QTextEdit* description_edit_;
  QComboBox* amplitude_unit_combo_;
};

#endif  // PROJECTPROPERTIESDIALOG_H
