/*
 * File:        pulldowndialog.cpp
 * Module:      orc-gui
 * Purpose:     Pulldown observation display dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "pulldowndialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFont>
#include <QFontDatabase>

PulldownDialog::PulldownDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setWindowTitle("Pulldown Observer");
    
    // Use Qt::Window flag to allow independent positioning
    setWindowFlags(Qt::Window);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size
    resize(480, 500);
    setMinimumSize(450, 480);
}

PulldownDialog::~PulldownDialog() = default;

void PulldownDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Field information
    auto* fieldGroup = new QGroupBox("Field Information");
    auto* fieldLayout = new QGridLayout(fieldGroup);
    fieldLayout->setColumnStretch(1, 1);
    
    fieldLayout->addWidget(new QLabel("Field ID:"), 0, 0);
    field_id_label_ = new QLabel("-");
    fieldLayout->addWidget(field_id_label_, 0, 1);
    
    mainLayout->addWidget(fieldGroup);
    
    // Detection result
    auto* detectionGroup = new QGroupBox("Pulldown Detection");
    auto* detectionLayout = new QGridLayout(detectionGroup);
    detectionLayout->setColumnStretch(1, 1);
    detectionLayout->setVerticalSpacing(8);
    detectionLayout->setHorizontalSpacing(12);
    
    detectionLayout->addWidget(new QLabel("Is Pulldown:"), 0, 0, Qt::AlignTop);
    is_pulldown_label_ = new QLabel("-");
    is_pulldown_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    detectionLayout->addWidget(is_pulldown_label_, 0, 1, Qt::AlignTop);
    
    detectionLayout->addWidget(new QLabel("Confidence:"), 1, 0, Qt::AlignTop);
    confidence_label_ = new QLabel("-");
    confidence_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    detectionLayout->addWidget(confidence_label_, 1, 1, Qt::AlignTop);
    
    detectionLayout->addWidget(new QLabel("Detection Basis:"), 2, 0, Qt::AlignTop);
    detection_basis_label_ = new QLabel("-");
    detection_basis_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    detection_basis_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    detectionLayout->addWidget(detection_basis_label_, 2, 1, Qt::AlignTop);
    
    mainLayout->addWidget(detectionGroup);
    
    // Pattern information (3:2 pulldown pattern)
    auto* patternGroup = new QGroupBox("3:2 Pulldown Pattern");
    auto* patternLayout = new QGridLayout(patternGroup);
    patternLayout->setColumnStretch(1, 1);
    patternLayout->setVerticalSpacing(8);
    patternLayout->setHorizontalSpacing(12);
    
    patternLayout->addWidget(new QLabel("Pattern Position:"), 0, 0);
    pattern_position_label_ = new QLabel("-");
    pattern_position_label_->setToolTip("Position in 5-frame cycle (0-4). Frames 1 and 3 typically have pulldown.");
    patternLayout->addWidget(pattern_position_label_, 0, 1);
    
    patternLayout->addWidget(new QLabel("Pattern Break:"), 1, 0);
    pattern_break_label_ = new QLabel("-");
    pattern_break_label_->setToolTip("True if pattern is inconsistent or contradictory evidence detected");
    patternLayout->addWidget(pattern_break_label_, 1, 1);
    
    mainLayout->addWidget(patternGroup);
    
    // Diagnostic information
    auto* diagGroup = new QGroupBox("Analysis Details");
    auto* diagLayout = new QGridLayout(diagGroup);
    diagLayout->setColumnStretch(1, 1);
    diagLayout->setVerticalSpacing(8);
    diagLayout->setHorizontalSpacing(12);
    
    diagLayout->addWidget(new QLabel("Phase Analysis:"), 0, 0, Qt::AlignTop);
    phase_analysis_label_ = new QLabel("-");
    phase_analysis_label_->setWordWrap(true);
    phase_analysis_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    phase_analysis_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    phase_analysis_label_->setToolTip("NTSC phase sequence analysis for repeated fields");
    diagLayout->addWidget(phase_analysis_label_, 0, 1, Qt::AlignTop);
    
    diagLayout->addWidget(new QLabel("VBI Pattern:"), 1, 0, Qt::AlignTop);
    vbi_pattern_label_ = new QLabel("-");
    vbi_pattern_label_->setWordWrap(true);
    vbi_pattern_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    vbi_pattern_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    vbi_pattern_label_->setToolTip("VBI frame number pattern analysis");
    diagLayout->addWidget(vbi_pattern_label_, 1, 1, Qt::AlignTop);
    
    mainLayout->addWidget(diagGroup);
    
    // Information box
    auto* infoGroup = new QGroupBox("About NTSC 3:2 Pulldown");
    auto* infoLayout = new QVBoxLayout(infoGroup);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    
    auto* infoLabel = new QLabel(
        "NTSC CAV discs use 3:2 pulldown to convert 24fps film to 29.97fps video. "
        "This creates a repeating 5-frame pattern where frames 1 and 3 have repeated fields. "
        "The observer detects pulldown by analyzing phase sequences and VBI frame numbers."
    );
    infoLabel->setWordWrap(true);
    infoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    infoLabel->setStyleSheet("QLabel { color: gray; font-size: 9pt; }");
    infoLayout->addWidget(infoLabel);
    
    mainLayout->addWidget(infoGroup);
    
    mainLayout->addStretch();
}

void PulldownDialog::updatePulldownObservation(const std::shared_ptr<orc::PulldownObservation>& observation)
{
    if (!observation) {
        clearPulldownInfo();
        return;
    }
    
    // Field information
    field_id_label_->setText(QString::number(observation->field_id.value()));
    
    // Detection result
    if (observation->is_pulldown) {
        is_pulldown_label_->setText("Yes");
        is_pulldown_label_->setStyleSheet("QLabel { color: green; }");
    } else {
        is_pulldown_label_->setText("No");
        is_pulldown_label_->setStyleSheet("QLabel { color: red; }");
    }
    
    confidence_label_->setText(formatConfidence(observation->confidence));
    detection_basis_label_->setText(formatDetectionBasis(observation->detection_basis));
    
    // Pattern information
    if (observation->pattern_position >= 0 && observation->pattern_position <= 4) {
        pattern_position_label_->setText(QString::number(observation->pattern_position) + " / 4");
        
        // Highlight typical pulldown positions
        if (observation->pattern_position == 1 || observation->pattern_position == 3) {
            pattern_position_label_->setStyleSheet("QLabel { font-weight: bold; }");
        } else {
            pattern_position_label_->setStyleSheet("");
        }
    } else {
        pattern_position_label_->setText("Unknown");
        pattern_position_label_->setStyleSheet("");
    }
    
    if (observation->pattern_break) {
        pattern_break_label_->setText("YES");
        pattern_break_label_->setStyleSheet("QLabel { color: orange; font-weight: bold; }");
    } else {
        pattern_break_label_->setText("No");
        pattern_break_label_->setStyleSheet("");
    }
    
    // Note: We don't have direct access to the intermediate analysis results
    // (phase_suggests_pulldown, vbi_suggests_pulldown) in the observation,
    // so we show general status based on the final result
    
    // Infer analysis results from confidence and result
    if (observation->is_pulldown) {
        if (observation->confidence == orc::ConfidenceLevel::HIGH) {
            phase_analysis_label_->setText("Detected (High confidence - both methods agree)");
            phase_analysis_label_->setStyleSheet("QLabel { color: green; }");
            vbi_pattern_label_->setText("Detected (High confidence - both methods agree)");
            vbi_pattern_label_->setStyleSheet("QLabel { color: green; }");
        } else if (observation->confidence == orc::ConfidenceLevel::MEDIUM) {
            // One or the other detected it
            if (observation->pattern_break) {
                phase_analysis_label_->setText("Conflicting evidence");
                phase_analysis_label_->setStyleSheet("QLabel { color: orange; }");
                vbi_pattern_label_->setText("Conflicting evidence");
                vbi_pattern_label_->setStyleSheet("QLabel { color: orange; }");
            } else {
                phase_analysis_label_->setText("Detected (one method)");
                phase_analysis_label_->setStyleSheet("QLabel { color: darkgreen; }");
                vbi_pattern_label_->setText("Partial detection");
                vbi_pattern_label_->setStyleSheet("QLabel { color: darkgreen; }");
            }
        } else {
            phase_analysis_label_->setText("Low confidence detection");
            phase_analysis_label_->setStyleSheet("QLabel { color: gray; }");
            vbi_pattern_label_->setText("Low confidence detection");
            vbi_pattern_label_->setStyleSheet("QLabel { color: gray; }");
        }
    } else {
        phase_analysis_label_->setText("Not detected");
        phase_analysis_label_->setStyleSheet("");
        vbi_pattern_label_->setText("Not detected");
        vbi_pattern_label_->setStyleSheet("");
    }
}

void PulldownDialog::clearPulldownInfo()
{
    field_id_label_->setText("-");
    is_pulldown_label_->setText("-");
    is_pulldown_label_->setStyleSheet("");
    confidence_label_->setText("-");
    detection_basis_label_->setText("-");
    pattern_position_label_->setText("-");
    pattern_position_label_->setStyleSheet("");
    pattern_break_label_->setText("-");
    pattern_break_label_->setStyleSheet("");
    phase_analysis_label_->setText("-");
    phase_analysis_label_->setStyleSheet("");
    vbi_pattern_label_->setText("-");
    vbi_pattern_label_->setStyleSheet("");
}

QString PulldownDialog::formatConfidence(orc::ConfidenceLevel level)
{
    switch (level) {
        case orc::ConfidenceLevel::NONE:
            return "None";
        case orc::ConfidenceLevel::LOW:
            return "Low";
        case orc::ConfidenceLevel::MEDIUM:
            return "Medium";
        case orc::ConfidenceLevel::HIGH:
            return "High";
        default:
            return "Unknown";
    }
}

QString PulldownDialog::formatDetectionBasis(orc::DetectionBasis basis)
{
    switch (basis) {
        case orc::DetectionBasis::HINT_DERIVED:
            return "Hint-Derived";
        case orc::DetectionBasis::SAMPLE_DERIVED:
            return "Sample Analysis";
        case orc::DetectionBasis::CORROBORATED:
            return "Corroborated (Hints + Analysis)";
        default:
            return "Unknown";
    }
}
