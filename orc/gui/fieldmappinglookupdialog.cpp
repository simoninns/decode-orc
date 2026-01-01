/*
 * File:        fieldmappinglookupdialog.cpp
 * Module:      orc-gui
 * Purpose:     Field mapping lookup dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "fieldmappinglookupdialog.h"
#include "../core/analysis/field_mapping/field_mapping_lookup.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QFont>

FieldMappingLookupDialog::FieldMappingLookupDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    updateUIState();
}

FieldMappingLookupDialog::~FieldMappingLookupDialog() = default;

void FieldMappingLookupDialog::setupUI() {
    setWindowTitle("Field Mapping Lookup");
    setMinimumSize(600, 500);
    
    auto *mainLayout = new QVBoxLayout(this);
    
    // Query type selector
    auto *queryTypeLayout = new QHBoxLayout();
    queryTypeLayout->addWidget(new QLabel("Query Type:"));
    queryTypeCombo_ = new QComboBox();
    queryTypeCombo_->addItem("Frame Number(s) → Field IDs");
    queryTypeCombo_->addItem("Timecode(s) → Field IDs");
    queryTypeCombo_->addItem("Field ID(s) → Frame/Timecode");
    queryTypeLayout->addWidget(queryTypeCombo_);
    queryTypeLayout->addStretch();
    mainLayout->addLayout(queryTypeLayout);
    
    // Frame query group
    frameQueryGroup_ = new QGroupBox("Frame Number Query");
    auto *frameLayout = new QFormLayout();
    
    auto *frameRangeLayout = new QHBoxLayout();
    frameStartEdit_ = new QLineEdit();
    frameStartEdit_->setPlaceholderText("1000");
    frameEndEdit_ = new QLineEdit();
    frameEndEdit_->setPlaceholderText("2000 (optional)");
    frameRangeLayout->addWidget(frameStartEdit_);
    frameRangeLayout->addWidget(new QLabel(" to "));
    frameRangeLayout->addWidget(frameEndEdit_);
    frameLayout->addRow("Frame Range:", frameRangeLayout);
    
    frameRangeLabel_ = new QLabel("Enter a single frame or a range. Frame numbers are typically 1-based for CAV discs.");
    frameRangeLabel_->setWordWrap(true);
    QFont smallFont = frameRangeLabel_->font();
    smallFont.setPointSize(smallFont.pointSize() - 1);
    frameRangeLabel_->setFont(smallFont);
    frameRangeLabel_->setStyleSheet("color: gray;");
    frameLayout->addRow("", frameRangeLabel_);
    
    frameQueryGroup_->setLayout(frameLayout);
    mainLayout->addWidget(frameQueryGroup_);
    
    // Timecode query group
    timecodeQueryGroup_ = new QGroupBox("Timecode Query");
    auto *timecodeLayout = new QFormLayout();
    
    auto *timecodeRangeLayout = new QHBoxLayout();
    timecodeStartEdit_ = new QLineEdit();
    timecodeStartEdit_->setPlaceholderText("0:10:10.28");
    timecodeEndEdit_ = new QLineEdit();
    timecodeEndEdit_->setPlaceholderText("0:20:10.03 (optional)");
    timecodeRangeLayout->addWidget(timecodeStartEdit_);
    timecodeRangeLayout->addWidget(new QLabel(" to "));
    timecodeRangeLayout->addWidget(timecodeEndEdit_);
    timecodeLayout->addRow("Timecode Range:", timecodeRangeLayout);
    
    timecodeFormatLabel_ = new QLabel("Format: H:MM:SS.FF (e.g., 0:10:10.28). CLV discs only.");
    timecodeFormatLabel_->setWordWrap(true);
    timecodeFormatLabel_->setFont(smallFont);
    timecodeFormatLabel_->setStyleSheet("color: gray;");
    timecodeLayout->addRow("", timecodeFormatLabel_);
    
    timecodeQueryGroup_->setLayout(timecodeLayout);
    mainLayout->addWidget(timecodeQueryGroup_);
    
    // Field ID query group
    fieldIdQueryGroup_ = new QGroupBox("Field ID Query");
    auto *fieldIdLayout = new QFormLayout();
    
    auto *fieldIdRangeLayout = new QHBoxLayout();
    fieldIdStartEdit_ = new QLineEdit();
    fieldIdStartEdit_->setPlaceholderText("5000");
    fieldIdEndEdit_ = new QLineEdit();
    fieldIdEndEdit_->setPlaceholderText("10000 (optional)");
    fieldIdRangeLayout->addWidget(fieldIdStartEdit_);
    fieldIdRangeLayout->addWidget(new QLabel(" to "));
    fieldIdRangeLayout->addWidget(fieldIdEndEdit_);
    fieldIdLayout->addRow("Field ID Range:", fieldIdRangeLayout);
    
    auto *fieldIdLabel = new QLabel("Enter a single field ID or a range to get frame/timecode info.");
    fieldIdLabel->setWordWrap(true);
    fieldIdLabel->setFont(smallFont);
    fieldIdLabel->setStyleSheet("color: gray;");
    fieldIdLayout->addRow("", fieldIdLabel);
    
    fieldIdQueryGroup_->setLayout(fieldIdLayout);
    mainLayout->addWidget(fieldIdQueryGroup_);
    
    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    lookupButton_ = new QPushButton("Lookup");
    lookupButton_->setDefault(true);
    clearButton_ = new QPushButton("Clear");
    auto *closeButton = new QPushButton("Close");
    buttonLayout->addWidget(lookupButton_);
    buttonLayout->addWidget(clearButton_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);
    
    // Results area
    mainLayout->addWidget(new QLabel("Results:"));
    resultsText_ = new QTextEdit();
    resultsText_->setReadOnly(true);
    resultsText_->setFont(QFont("Monospace", 9));
    mainLayout->addWidget(resultsText_);
    
    // Status label
    statusLabel_ = new QLabel();
    statusLabel_->setFont(smallFont);
    statusLabel_->setStyleSheet("color: gray;");
    mainLayout->addWidget(statusLabel_);
    
    // Connections
    connect(queryTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FieldMappingLookupDialog::onQueryTypeChanged);
    connect(lookupButton_, &QPushButton::clicked,
            this, &FieldMappingLookupDialog::onLookupButtonClicked);
    connect(clearButton_, &QPushButton::clicked,
            this, &FieldMappingLookupDialog::onClearButtonClicked);
    connect(closeButton, &QPushButton::clicked,
            this, &QDialog::accept);
}

void FieldMappingLookupDialog::setLookup(std::shared_ptr<orc::FieldMappingLookup> lookup) {
    lookup_ = lookup;
    updateUIState();
    
    if (lookup_) {
        QString format = QString("%1 (%2)")
            .arg(lookup_->is_cav() ? "CAV" : "CLV")
            .arg(lookup_->is_pal() ? "PAL" : "NTSC");
        
        statusLabel_->setText(QString("Source: %1, %2 frames, %3 fields")
            .arg(format)
            .arg(lookup_->get_frame_count())
            .arg(lookup_->get_field_range().size()));
    } else {
        statusLabel_->setText("No source loaded");
    }
}

void FieldMappingLookupDialog::clearLookup() {
    lookup_.reset();
    updateUIState();
    statusLabel_->setText("No source loaded");
    resultsText_->clear();
}

void FieldMappingLookupDialog::onQueryTypeChanged(int index) {
    frameQueryGroup_->setVisible(index == 0);
    timecodeQueryGroup_->setVisible(index == 1);
    fieldIdQueryGroup_->setVisible(index == 2);
    
    // Update help text for timecode based on format
    if (lookup_ && index == 1) {
        if (lookup_->is_clv()) {
            timecodeFormatLabel_->setText("Format: H:MM:SS.FF (e.g., 0:10:10.28). CLV format detected.");
            timecodeFormatLabel_->setStyleSheet("color: green;");
        } else {
            timecodeFormatLabel_->setText("⚠ This source is CAV (frame-numbered), not CLV. Timecode queries not available.");
            timecodeFormatLabel_->setStyleSheet("color: red;");
        }
    }
}

void FieldMappingLookupDialog::onLookupButtonClicked() {
    if (!lookup_) {
        displayError("No lookup data available. Please load a source first.");
        return;
    }
    
    int queryType = queryTypeCombo_->currentIndex();
    
    switch (queryType) {
        case 0:
            performFrameLookup();
            break;
        case 1:
            performTimecodeLookup();
            break;
        case 2:
            performFieldIdLookup();
            break;
    }
}

void FieldMappingLookupDialog::onClearButtonClicked() {
    resultsText_->clear();
    frameStartEdit_->clear();
    frameEndEdit_->clear();
    timecodeStartEdit_->clear();
    timecodeEndEdit_->clear();
    fieldIdStartEdit_->clear();
    fieldIdEndEdit_->clear();
}

void FieldMappingLookupDialog::updateUIState() {
    bool hasLookup = (lookup_ != nullptr);
    lookupButton_->setEnabled(hasLookup);
    queryTypeCombo_->setEnabled(hasLookup);
    frameQueryGroup_->setEnabled(hasLookup);
    timecodeQueryGroup_->setEnabled(hasLookup);
    fieldIdQueryGroup_->setEnabled(hasLookup);
    
    onQueryTypeChanged(queryTypeCombo_->currentIndex());
}

void FieldMappingLookupDialog::performFrameLookup() {
    QString startText = frameStartEdit_->text().trimmed();
    QString endText = frameEndEdit_->text().trimmed();
    
    if (startText.isEmpty()) {
        displayError("Please enter a frame number.");
        return;
    }
    
    bool ok;
    int32_t startFrame = startText.toInt(&ok);
    if (!ok) {
        displayError("Invalid frame number: " + startText);
        return;
    }
    
    orc::FieldLookupResult result;
    
    if (endText.isEmpty()) {
        // Single frame query
        result = lookup_->get_fields_for_frame(startFrame, true);  // Assume 1-based
    } else {
        int32_t endFrame = endText.toInt(&ok);
        if (!ok) {
            displayError("Invalid frame number: " + endText);
            return;
        }
        result = lookup_->get_fields_for_frame_range(startFrame, endFrame, true);
    }
    
    if (!result.success) {
        displayError(QString::fromStdString(result.error_message));
        return;
    }
    
    QString output;
    output += "=== Frame Lookup Results ===\n\n";
    output += QString("Query: Frame %1").arg(startText);
    if (!endText.isEmpty()) {
        output += QString(" - %1").arg(endText);
    }
    output += "\n\n";
    
    output += QString("Format: %1 (%2)\n")
        .arg(result.is_cav ? "CAV" : "CLV")
        .arg(result.is_pal ? "PAL" : "NTSC");
    
    output += QString("Field ID Range: %1 - %2\n")
        .arg(result.start_field_id.value())
        .arg(result.end_field_id.value());
    
    output += QString("Total Fields: %1\n")
        .arg(result.end_field_id.value() - result.start_field_id.value());
    
    if (result.picture_number.has_value()) {
        output += QString("CAV Picture Number: %1\n").arg(*result.picture_number);
    }
    
    if (result.timecode.has_value()) {
        output += QString("CLV Timecode: %1\n")
            .arg(QString::fromStdString(result.timecode->to_string()));
    }
    
    displayResult(output);
}

void FieldMappingLookupDialog::performTimecodeLookup() {
    if (!lookup_->is_clv()) {
        displayError("Timecode queries are only available for CLV sources. This source is CAV.");
        return;
    }
    
    QString startText = timecodeStartEdit_->text().trimmed();
    QString endText = timecodeEndEdit_->text().trimmed();
    
    if (startText.isEmpty()) {
        displayError("Please enter a timecode.");
        return;
    }
    
    auto startTc = orc::FieldMappingLookup::parse_timecode(startText.toStdString());
    if (!startTc) {
        displayError("Invalid timecode format: " + startText + "\nExpected: H:MM:SS.FF");
        return;
    }
    
    orc::FieldLookupResult result;
    
    if (endText.isEmpty()) {
        // Single timecode query
        result = lookup_->get_fields_for_timecode(*startTc);
    } else {
        auto endTc = orc::FieldMappingLookup::parse_timecode(endText.toStdString());
        if (!endTc) {
            displayError("Invalid timecode format: " + endText + "\nExpected: H:MM:SS.FF");
            return;
        }
        result = lookup_->get_fields_for_timecode_range(*startTc, *endTc);
    }
    
    if (!result.success) {
        displayError(QString::fromStdString(result.error_message));
        return;
    }
    
    QString output;
    output += "=== Timecode Lookup Results ===\n\n";
    output += QString("Query: Timecode %1").arg(startText);
    if (!endText.isEmpty()) {
        output += QString(" - %1").arg(endText);
    }
    output += "\n\n";
    
    output += QString("Format: CLV (%1)\n").arg(result.is_pal ? "PAL" : "NTSC");
    
    output += QString("Field ID Range: %1 - %2\n")
        .arg(result.start_field_id.value())
        .arg(result.end_field_id.value());
    
    output += QString("Total Fields: %1\n")
        .arg(result.end_field_id.value() - result.start_field_id.value());
    
    displayResult(output);
}

void FieldMappingLookupDialog::performFieldIdLookup() {
    QString startText = fieldIdStartEdit_->text().trimmed();
    QString endText = fieldIdEndEdit_->text().trimmed();
    
    if (startText.isEmpty()) {
        displayError("Please enter a field ID.");
        return;
    }
    
    bool ok;
    uint64_t startFieldId = startText.toULongLong(&ok);
    if (!ok) {
        displayError("Invalid field ID: " + startText);
        return;
    }
    
    QString output;
    output += "=== Field ID Lookup Results ===\n\n";
    
    if (endText.isEmpty()) {
        // Single field ID query
        auto result = lookup_->get_info_for_field(orc::FieldID(startFieldId));
        
        if (!result.success) {
            displayError(QString::fromStdString(result.error_message));
            return;
        }
        
        output += QString("Query: Field ID %1\n\n").arg(startFieldId);
        output += QString("Format: %1 (%2)\n")
            .arg(result.is_cav ? "CAV" : "CLV")
            .arg(result.is_pal ? "PAL" : "NTSC");
        
        if (result.picture_number.has_value()) {
            output += QString("CAV Picture Number: %1\n").arg(*result.picture_number);
        }
        
        if (result.timecode.has_value()) {
            output += QString("CLV Timecode: %1\n")
                .arg(QString::fromStdString(result.timecode->to_string()));
        }
        
    } else {
        // Range query
        uint64_t endFieldId = endText.toULongLong(&ok);
        if (!ok) {
            displayError("Invalid field ID: " + endText);
            return;
        }
        
        auto startResult = lookup_->get_info_for_field(orc::FieldID(startFieldId));
        auto endResult = lookup_->get_info_for_field(orc::FieldID(endFieldId));
        
        output += QString("Query: Field IDs %1 - %2\n\n").arg(startFieldId).arg(endFieldId);
        
        if (startResult.success) {
            output += QString("Start Field %1:\n").arg(startFieldId);
            if (startResult.picture_number.has_value()) {
                output += QString("  CAV Frame: %1\n").arg(*startResult.picture_number);
            }
            if (startResult.timecode.has_value()) {
                output += QString("  CLV Timecode: %1\n")
                    .arg(QString::fromStdString(startResult.timecode->to_string()));
            }
            output += "\n";
        }
        
        if (endResult.success) {
            output += QString("End Field %1:\n").arg(endFieldId);
            if (endResult.picture_number.has_value()) {
                output += QString("  CAV Frame: %1\n").arg(*endResult.picture_number);
            }
            if (endResult.timecode.has_value()) {
                output += QString("  CLV Timecode: %1\n")
                    .arg(QString::fromStdString(endResult.timecode->to_string()));
            }
        }
    }
    
    displayResult(output);
}

void FieldMappingLookupDialog::displayResult(const QString& result) {
    resultsText_->setPlainText(result);
    resultsText_->setStyleSheet("");  // Clear any error styling
}

void FieldMappingLookupDialog::displayError(const QString& error) {
    resultsText_->setPlainText("ERROR: " + error);
    resultsText_->setStyleSheet("color: red;");
}
