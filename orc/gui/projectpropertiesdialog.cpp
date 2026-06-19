/*
 * File:        projectpropertiesdialog.cpp
 * Module:      orc-gui
 * Purpose:     Dialog for editing project properties
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "projectpropertiesdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>

ProjectPropertiesDialog::ProjectPropertiesDialog(QWidget* parent)
    : QDialog(parent),
      name_edit_(nullptr),
      description_edit_(nullptr),
      amplitude_unit_combo_(nullptr) {
  setWindowTitle("Project Properties");
  setupUI();
  resize(500, 300);
}

ProjectPropertiesDialog::~ProjectPropertiesDialog() {}

void ProjectPropertiesDialog::setupUI() {
  auto* main_layout = new QVBoxLayout(this);

  // Form layout for project properties
  auto* form_layout = new QFormLayout();

  // Project name (single line)
  name_edit_ = new QLineEdit(this);
  name_edit_->setPlaceholderText("Enter project name");
  form_layout->addRow("Project Name:", name_edit_);

  // Project description (multi-line)
  description_edit_ = new QTextEdit(this);
  description_edit_->setPlaceholderText("Enter project description (optional)");
  description_edit_->setAcceptRichText(false);  // Plain text only
  form_layout->addRow("Description:", description_edit_);

  // Amplitude display unit selector
  amplitude_unit_combo_ = new QComboBox(this);
  amplitude_unit_combo_->addItem("IRE");
  amplitude_unit_combo_->addItem("mV (millivolts)");
  amplitude_unit_combo_->addItem("10-bit samples");
  form_layout->addRow("Amplitude Unit:", amplitude_unit_combo_);

  main_layout->addLayout(form_layout);

  // Dialog buttons
  auto* button_box = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

  main_layout->addWidget(button_box);
}

QString ProjectPropertiesDialog::projectName() const {
  return name_edit_->text().trimmed();
}

void ProjectPropertiesDialog::setProjectName(const QString& name) {
  name_edit_->setText(name);
}

QString ProjectPropertiesDialog::projectDescription() const {
  return description_edit_->toPlainText().trimmed();
}

void ProjectPropertiesDialog::setProjectDescription(
    const QString& description) {
  description_edit_->setPlainText(description);
}

orc::AmplitudeDisplayUnit ProjectPropertiesDialog::amplitudeUnit() const {
  switch (amplitude_unit_combo_->currentIndex()) {
    case 1:
      return orc::AmplitudeDisplayUnit::Millivolts;
    case 2:
      return orc::AmplitudeDisplayUnit::Samples10Bit;
    default:
      return orc::AmplitudeDisplayUnit::IRE;
  }
}

void ProjectPropertiesDialog::setAmplitudeUnit(orc::AmplitudeDisplayUnit unit) {
  switch (unit) {
    case orc::AmplitudeDisplayUnit::Millivolts:
      amplitude_unit_combo_->setCurrentIndex(1);
      break;
    case orc::AmplitudeDisplayUnit::Samples10Bit:
      amplitude_unit_combo_->setCurrentIndex(2);
      break;
    default:
      amplitude_unit_combo_->setCurrentIndex(0);
      break;
  }
}
