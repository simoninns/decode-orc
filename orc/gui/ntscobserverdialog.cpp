/*
 * File:        ntscobserverdialog.cpp
 * Module:      orc-gui
 * Purpose:     NTSC observer display dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "ntscobserverdialog.h"
#include "logging.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFont>
#include <QFontDatabase>

NtscObserverDialog::NtscObserverDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setWindowTitle("NTSC Observer");
    
    // Use Qt::Window flag to allow independent positioning
    setWindowFlags(Qt::Window);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size
    resize(480, 400);
    setMinimumSize(450, 380);
}

NtscObserverDialog::~NtscObserverDialog() = default;

void NtscObserverDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Field 1 group (FM Code and White Flag)
    field1_group_ = new QGroupBox("Field 1", this);
    auto* field1Layout = new QGridLayout(field1_group_);
    field1Layout->setColumnStretch(1, 1);
    field1Layout->setVerticalSpacing(8);
    field1Layout->setHorizontalSpacing(12);
    
    // FM Code section
    field1Layout->addWidget(new QLabel("FM Code:"), 0, 0);
    field1_fm_code_present_label_ = new QLabel("-");
    field1Layout->addWidget(field1_fm_code_present_label_, 0, 1);
    
    field1Layout->addWidget(new QLabel("  Data:"), 1, 0);
    field1_fm_code_data_label_ = new QLabel("-");
    field1_fm_code_data_label_->setToolTip("20-bit FM code payload");
    field1Layout->addWidget(field1_fm_code_data_label_, 1, 1);
    
    field1Layout->addWidget(new QLabel("  Field Flag:"), 2, 0);
    field1_fm_code_field_flag_label_ = new QLabel("-");
    field1_fm_code_field_flag_label_->setToolTip("Field indicator bit");
    field1Layout->addWidget(field1_fm_code_field_flag_label_, 2, 1);
    
    // White Flag section
    field1Layout->addWidget(new QLabel("White Flag:"), 3, 0);
    field1_white_flag_present_label_ = new QLabel("-");
    field1_white_flag_present_label_->setToolTip("White flag detected on this field");
    field1Layout->addWidget(field1_white_flag_present_label_, 3, 1);
    
    mainLayout->addWidget(field1_group_);
    
    // Field 2 group (FM Code and White Flag)
    field2_group_ = new QGroupBox("Field 2", this);
    auto* field2Layout = new QGridLayout(field2_group_);
    field2Layout->setColumnStretch(1, 1);
    field2Layout->setVerticalSpacing(8);
    field2Layout->setHorizontalSpacing(12);
    
    // FM Code section
    field2Layout->addWidget(new QLabel("FM Code:"), 0, 0);
    field2_fm_code_present_label_ = new QLabel("-");
    field2Layout->addWidget(field2_fm_code_present_label_, 0, 1);
    
    field2Layout->addWidget(new QLabel("  Data:"), 1, 0);
    field2_fm_code_data_label_ = new QLabel("-");
    field2_fm_code_data_label_->setToolTip("20-bit FM code payload");
    field2Layout->addWidget(field2_fm_code_data_label_, 1, 1);
    
    field2Layout->addWidget(new QLabel("  Field Flag:"), 2, 0);
    field2_fm_code_field_flag_label_ = new QLabel("-");
    field2_fm_code_field_flag_label_->setToolTip("Field indicator bit");
    field2Layout->addWidget(field2_fm_code_field_flag_label_, 2, 1);
    
    // White Flag section
    field2Layout->addWidget(new QLabel("White Flag:"), 3, 0);
    field2_white_flag_present_label_ = new QLabel("-");
    field2_white_flag_present_label_->setToolTip("White flag detected on this field");
    field2Layout->addWidget(field2_white_flag_present_label_, 3, 1);
    
    mainLayout->addWidget(field2_group_);
    
    // Initially hide field 2 (will show when in frame mode)
    field2_group_->hide();
    
    mainLayout->addStretch();
    
    showing_frame_mode_ = false;
}

void NtscObserverDialog::updateObservations(const orc::FieldID& field_id, const orc::presenters::NtscFieldObservationsView& observations)
{
    showing_frame_mode_ = false;
    field1_group_->show();
    field2_group_->hide();
    field1_group_->setTitle("Field Metrics");
    
    updateFieldObservations(field1_group_, "Field 1",
                           field1_fm_code_present_label_, field1_fm_code_data_label_, field1_fm_code_field_flag_label_,
                           field1_white_flag_present_label_, observations);
}

void NtscObserverDialog::updateObservationsForFrame(const orc::FieldID& field1_id,
                                                   const orc::presenters::NtscFieldObservationsView& field1_observations,
                                                   const orc::FieldID& field2_id,
                                                   const orc::presenters::NtscFieldObservationsView& field2_observations)
{
    showing_frame_mode_ = true;
    field1_group_->show();
    field2_group_->show();
    field1_group_->setTitle("Field 1");
    
    updateFieldObservations(field1_group_, "Field 1",
                           field1_fm_code_present_label_, field1_fm_code_data_label_, field1_fm_code_field_flag_label_,
                           field1_white_flag_present_label_, field1_observations);
    
    updateFieldObservations(field2_group_, "Field 2",
                           field2_fm_code_present_label_, field2_fm_code_data_label_, field2_fm_code_field_flag_label_,
                           field2_white_flag_present_label_, field2_observations);
}

void NtscObserverDialog::updateFieldObservations(QGroupBox* field_group, const QString& field_label,
                                                QLabel* fm_present, QLabel* fm_data, QLabel* fm_flag,
                                                QLabel* white_flag,
                                                const orc::presenters::NtscFieldObservationsView& observations)
{
    // Display FM code present
    if (observations.fm_code) {
        const auto& fm_code = *observations.fm_code;
        fm_present->setText(fm_code.present ? "Yes" : "No");
        fm_present->setStyleSheet(fm_code.present ? "QLabel { color: #00AA00; font-weight: bold; }" : "");
        
        // Display FM code data value
        fm_data->setText(QString("0x%1 (%2)")
            .arg(fm_code.data_value, 5, 16, QChar('0'))
            .arg(fm_code.data_value));
        
        // Display FM code field flag
        fm_flag->setText(fm_code.field_flag ? "True" : "False");
    } else {
        fm_present->setText("No");
        fm_present->setStyleSheet("");
        fm_data->setText("-");
        fm_flag->setText("-");
    }
    
    // Display white flag present
    if (observations.white_flag) {
        const auto& wf = *observations.white_flag;
        white_flag->setText(wf.present ? "Yes" : "No");
        white_flag->setStyleSheet(wf.present ? "QLabel { color: #00AA00; font-weight: bold; }" : "");
    } else {
        white_flag->setText("No");
        white_flag->setStyleSheet("");
    }
}

void NtscObserverDialog::clearObservations()
{
    field1_fm_code_present_label_->setText("-");
    field1_fm_code_present_label_->setStyleSheet("");
    field1_fm_code_data_label_->setText("-");
    field1_fm_code_field_flag_label_->setText("-");
    field1_white_flag_present_label_->setText("-");
    field1_white_flag_present_label_->setStyleSheet("");
    
    field2_fm_code_present_label_->setText("-");
    field2_fm_code_present_label_->setStyleSheet("");
    field2_fm_code_data_label_->setText("-");
    field2_fm_code_field_flag_label_->setText("-");
    field2_white_flag_present_label_->setText("-");
    field2_white_flag_present_label_->setStyleSheet("");
}

