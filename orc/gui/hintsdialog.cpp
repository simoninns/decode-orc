/*
 * File:        hintsdialog.cpp
 * Module:      orc-gui
 * Purpose:     Video parameter hints display dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "hintsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFont>
#include <QFontDatabase>

HintsDialog::HintsDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setWindowTitle("Video Parameter Hints");
    
    // Use Qt::Window flag to allow independent positioning
    setWindowFlags(Qt::Window);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size
    resize(500, 550);
}

HintsDialog::~HintsDialog() = default;

void HintsDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Field Parity Hint
    auto* fieldParityGroup = new QGroupBox("Field Parity Hint");
    auto* fieldParityLayout = new QGridLayout(fieldParityGroup);
    
    fieldParityLayout->addWidget(new QLabel("Is First Field:"), 0, 0);
    field_parity_value_label_ = new QLabel("-");
    fieldParityLayout->addWidget(field_parity_value_label_, 0, 1);
    
    fieldParityLayout->addWidget(new QLabel("Source:"), 1, 0);
    field_parity_source_label_ = new QLabel("-");
    fieldParityLayout->addWidget(field_parity_source_label_, 1, 1);
    
    fieldParityLayout->addWidget(new QLabel("Confidence:"), 2, 0);
    field_parity_confidence_label_ = new QLabel("-");
    fieldParityLayout->addWidget(field_parity_confidence_label_, 2, 1);
    
    mainLayout->addWidget(fieldParityGroup);
    
    // Field Phase Hint
    auto* fieldPhaseGroup = new QGroupBox("Field Phase Hint");
    auto* fieldPhaseLayout = new QGridLayout(fieldPhaseGroup);
    
    fieldPhaseLayout->addWidget(new QLabel("Phase ID:"), 0, 0);
    field_phase_value_label_ = new QLabel("-");
    fieldPhaseLayout->addWidget(field_phase_value_label_, 0, 1);
    
    fieldPhaseLayout->addWidget(new QLabel("Source:"), 1, 0);
    field_phase_source_label_ = new QLabel("-");
    fieldPhaseLayout->addWidget(field_phase_source_label_, 1, 1);
    
    fieldPhaseLayout->addWidget(new QLabel("Confidence:"), 2, 0);
    field_phase_confidence_label_ = new QLabel("-");
    fieldPhaseLayout->addWidget(field_phase_confidence_label_, 2, 1);
    
    mainLayout->addWidget(fieldPhaseGroup);
    
    // Active Line Hint
    auto* activeLineGroup = new QGroupBox("Active Line Hint");
    auto* activeLineLayout = new QGridLayout(activeLineGroup);
    
    activeLineLayout->addWidget(new QLabel("Active Lines:"), 0, 0);
    active_line_value_label_ = new QLabel("-");
    activeLineLayout->addWidget(active_line_value_label_, 0, 1);
    
    activeLineLayout->addWidget(new QLabel("Source:"), 1, 0);
    active_line_source_label_ = new QLabel("-");
    activeLineLayout->addWidget(active_line_source_label_, 1, 1);
    
    activeLineLayout->addWidget(new QLabel("Confidence:"), 2, 0);
    active_line_confidence_label_ = new QLabel("-");
    activeLineLayout->addWidget(active_line_confidence_label_, 2, 1);
    
    mainLayout->addWidget(activeLineGroup);
    
    // Video Parameters (from metadata)
    auto* videoParamsGroup = new QGroupBox("Video Parameters (from Metadata)");
    auto* videoParamsLayout = new QGridLayout(videoParamsGroup);
    
    videoParamsLayout->addWidget(new QLabel("Active Video:"), 0, 0);
    active_video_range_label_ = new QLabel("-");
    videoParamsLayout->addWidget(active_video_range_label_, 0, 1);
    
    videoParamsLayout->addWidget(new QLabel("Colour Burst:"), 1, 0);
    colour_burst_range_label_ = new QLabel("-");
    videoParamsLayout->addWidget(colour_burst_range_label_, 1, 1);
    
    videoParamsLayout->addWidget(new QLabel("IRE Levels:"), 2, 0);
    ire_levels_label_ = new QLabel("-");
    videoParamsLayout->addWidget(ire_levels_label_, 2, 1);
    
    videoParamsLayout->addWidget(new QLabel("Sample Rate:"), 3, 0);
    sample_rate_label_ = new QLabel("-");
    videoParamsLayout->addWidget(sample_rate_label_, 3, 1);
    
    mainLayout->addWidget(videoParamsGroup);
    
    // Add info text
    auto* infoLabel = new QLabel(
        "Hints are video parameter information from upstream sources "
        "(e.g., ld-decode metadata) or derived from analysis. "
        "They guide processing stages in making decisions about field ordering, "
        "color phase, and other video parameters."
    );
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { color: gray; font-size: 9pt; }");
    mainLayout->addWidget(infoLabel);
    
    mainLayout->addStretch();
}

void HintsDialog::updateFieldParityHint(const std::optional<orc::FieldParityHint>& hint)
{
    if (!hint.has_value()) {
        field_parity_value_label_->setText("-");
        field_parity_source_label_->setText("-");
        field_parity_confidence_label_->setText("-");
        return;
    }
    
    // Format the value
    field_parity_value_label_->setText(hint->is_first_field ? "Yes (Field 1)" : "No (Field 2)");
    
    // Format the source
    field_parity_source_label_->setText(formatHintSource(hint->source));
    
    // Format the confidence
    field_parity_confidence_label_->setText(QString("%1%").arg(hint->confidence_pct));
}

void HintsDialog::updateFieldPhaseHint(const std::optional<orc::FieldPhaseHint>& hint)
{
    if (!hint.has_value()) {
        field_phase_value_label_->setText("-");
        field_phase_source_label_->setText("-");
        field_phase_confidence_label_->setText("-");
        return;
    }
    
    // Format the value
    if (hint->field_phase_id == -1) {
        field_phase_value_label_->setText("Unknown");
    } else {
        field_phase_value_label_->setText(QString::number(hint->field_phase_id));
    }
    
    // Format the source
    field_phase_source_label_->setText(formatHintSource(hint->source));
    
    // Format the confidence
    field_phase_confidence_label_->setText(QString("%1%").arg(hint->confidence_pct));
}

void HintsDialog::updateActiveLineHint(const std::optional<orc::ActiveLineHint>& hint)
{
    if (!hint.has_value() || !hint->is_valid()) {
        active_line_value_label_->setText("-");
        active_line_source_label_->setText("-");
        active_line_confidence_label_->setText("-");
        return;
    }
    
    // Format the value - show frame line range
    QString value = QString("Frame Lines %1-%2")
        .arg(hint->first_active_frame_line)
        .arg(hint->last_active_frame_line);
    active_line_value_label_->setText(value);
    
    // Format the source
    active_line_source_label_->setText(formatHintSource(hint->source));
    
    // Format the confidence
    active_line_confidence_label_->setText(QString("%1%").arg(hint->confidence_pct));
}

void HintsDialog::updateVideoParameters(const std::optional<orc::VideoParameters>& params)
{
    if (!params.has_value()) {
        active_video_range_label_->setText("-");
        colour_burst_range_label_->setText("-");
        ire_levels_label_->setText("-");
        sample_rate_label_->setText("-");
        return;
    }
    
    // Active video range
    if (params->active_video_start >= 0 && params->active_video_end >= 0) {
        active_video_range_label_->setText(
            QString("Samples %1-%2").arg(params->active_video_start).arg(params->active_video_end));
    } else {
        active_video_range_label_->setText("-");
    }
    
    // Colour burst range
    if (params->colour_burst_start >= 0 && params->colour_burst_end >= 0) {
        colour_burst_range_label_->setText(
            QString("Samples %1-%2").arg(params->colour_burst_start).arg(params->colour_burst_end));
    } else {
        colour_burst_range_label_->setText("-");
    }
    
    // IRE levels
    if (params->white_16b_ire >= 0 && params->black_16b_ire >= 0) {
        ire_levels_label_->setText(
            QString("White: %1, Black: %2 (16-bit)").arg(params->white_16b_ire).arg(params->black_16b_ire));
    } else {
        ire_levels_label_->setText("-");
    }
    
    // Sample rate
    if (params->sample_rate > 0) {
        sample_rate_label_->setText(QString("%1 Hz").arg(params->sample_rate, 0, 'f', 0));
    } else {
        sample_rate_label_->setText("-");
    }
}

void HintsDialog::clearHints()
{
    field_parity_value_label_->setText("-");
    field_parity_source_label_->setText("-");
    field_parity_confidence_label_->setText("-");
    
    field_phase_value_label_->setText("-");
    field_phase_source_label_->setText("-");
    field_phase_confidence_label_->setText("-");
    
    active_line_value_label_->setText("-");
    active_line_source_label_->setText("-");
    active_line_confidence_label_->setText("-");
    
    active_video_range_label_->setText("-");
    colour_burst_range_label_->setText("-");
    ire_levels_label_->setText("-");
    sample_rate_label_->setText("-");
}

QString HintsDialog::formatHintSource(orc::HintSource source)
{
    switch (source) {
        case orc::HintSource::METADATA:
            return "Metadata";
        case orc::HintSource::USER_OVERRIDE:
            return "User Override";
        case orc::HintSource::INHERITED:
            return "Inherited";
        case orc::HintSource::SAMPLE_ANALYSIS:
            return "Sample Analysis";
        case orc::HintSource::CORROBORATED:
            return "Corroborated";
        default:
            return "Unknown";
    }
}
