/*
 * File:        qualitymetricsdialog.cpp
 * Module:      orc-gui
 * Purpose:     Quality metrics dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "qualitymetricsdialog.h"
#include "../core/include/video_field_representation.h"
#include "../core/include/observation_context.h"
#include "../core/include/node_id.h"
#include "logging.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <cmath>

QualityMetricsDialog::QualityMetricsDialog(QWidget *parent)
    : QDialog(parent)
    , showing_frame_mode_(false)
{
    setupUI();
    setWindowTitle("Field/Frame Quality Metrics");
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size
    resize(500, 400);
}

QualityMetricsDialog::~QualityMetricsDialog() = default;

void QualityMetricsDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Field 1 metrics group
    field1_group_ = new QGroupBox("Field 1", this);
    auto* field1Layout = new QGridLayout(field1_group_);
    
    field1Layout->addWidget(new QLabel("White SNR:"), 0, 0);
    field1_white_snr_label_ = new QLabel("N/A");
    field1Layout->addWidget(field1_white_snr_label_, 0, 1);
    
    field1Layout->addWidget(new QLabel("Black PSNR:"), 1, 0);
    field1_black_psnr_label_ = new QLabel("N/A");
    field1Layout->addWidget(field1_black_psnr_label_, 1, 1);
    
    field1Layout->addWidget(new QLabel("Burst Level:"), 2, 0);
    field1_burst_level_label_ = new QLabel("N/A");
    field1Layout->addWidget(field1_burst_level_label_, 2, 1);
    
    field1Layout->addWidget(new QLabel("Quality Score:"), 3, 0);
    field1_quality_score_label_ = new QLabel("N/A");
    field1Layout->addWidget(field1_quality_score_label_, 3, 1);
    
    field1Layout->addWidget(new QLabel("Dropout Count:"), 4, 0);
    field1_dropout_count_label_ = new QLabel("N/A");
    field1Layout->addWidget(field1_dropout_count_label_, 4, 1);
    
    mainLayout->addWidget(field1_group_);
    
    // Field 2 metrics group
    field2_group_ = new QGroupBox("Field 2", this);
    auto* field2Layout = new QGridLayout(field2_group_);
    
    field2Layout->addWidget(new QLabel("White SNR:"), 0, 0);
    field2_white_snr_label_ = new QLabel("N/A");
    field2Layout->addWidget(field2_white_snr_label_, 0, 1);
    
    field2Layout->addWidget(new QLabel("Black PSNR:"), 1, 0);
    field2_black_psnr_label_ = new QLabel("N/A");
    field2Layout->addWidget(field2_black_psnr_label_, 1, 1);
    
    field2Layout->addWidget(new QLabel("Burst Level:"), 2, 0);
    field2_burst_level_label_ = new QLabel("N/A");
    field2Layout->addWidget(field2_burst_level_label_, 2, 1);
    
    field2Layout->addWidget(new QLabel("Quality Score:"), 3, 0);
    field2_quality_score_label_ = new QLabel("N/A");
    field2Layout->addWidget(field2_quality_score_label_, 3, 1);
    
    field2Layout->addWidget(new QLabel("Dropout Count:"), 4, 0);
    field2_dropout_count_label_ = new QLabel("N/A");
    field2Layout->addWidget(field2_dropout_count_label_, 4, 1);
    
    mainLayout->addWidget(field2_group_);
    
    // Frame average metrics group
    frame_group_ = new QGroupBox("Frame Average", this);
    auto* frameLayout = new QGridLayout(frame_group_);
    
    frameLayout->addWidget(new QLabel("White SNR:"), 0, 0);
    frame_white_snr_label_ = new QLabel("N/A");
    frameLayout->addWidget(frame_white_snr_label_, 0, 1);
    
    frameLayout->addWidget(new QLabel("Black PSNR:"), 1, 0);
    frame_black_psnr_label_ = new QLabel("N/A");
    frameLayout->addWidget(frame_black_psnr_label_, 1, 1);
    
    frameLayout->addWidget(new QLabel("Burst Level:"), 2, 0);
    frame_burst_level_label_ = new QLabel("N/A");
    frameLayout->addWidget(frame_burst_level_label_, 2, 1);
    
    frameLayout->addWidget(new QLabel("Quality Score:"), 3, 0);
    frame_quality_score_label_ = new QLabel("N/A");
    frameLayout->addWidget(frame_quality_score_label_, 3, 1);
    
    frameLayout->addWidget(new QLabel("Total Dropouts:"), 4, 0);
    frame_dropout_count_label_ = new QLabel("N/A");
    frameLayout->addWidget(frame_dropout_count_label_, 4, 1);
    
    mainLayout->addWidget(frame_group_);
    
    // Initially hide field 2 and frame groups
    field2_group_->hide();
    frame_group_->hide();
    
    mainLayout->addStretch();
}

void QualityMetricsDialog::updateFieldLabels(const FieldMetrics& metrics, bool is_field1)
{
    QLabel* white_snr_label = is_field1 ? field1_white_snr_label_ : field2_white_snr_label_;
    QLabel* black_psnr_label = is_field1 ? field1_black_psnr_label_ : field2_black_psnr_label_;
    QLabel* burst_level_label = is_field1 ? field1_burst_level_label_ : field2_burst_level_label_;
    QLabel* quality_score_label = is_field1 ? field1_quality_score_label_ : field2_quality_score_label_;
    QLabel* dropout_count_label = is_field1 ? field1_dropout_count_label_ : field2_dropout_count_label_;
    
    if (metrics.has_white_snr) {
        white_snr_label->setText(QString("%1 dB").arg(metrics.white_snr, 0, 'f', 2));
    } else {
        white_snr_label->setText("N/A");
    }
    
    if (metrics.has_black_psnr) {
        black_psnr_label->setText(QString("%1 dB").arg(metrics.black_psnr, 0, 'f', 2));
    } else {
        black_psnr_label->setText("N/A");
    }
    
    if (metrics.has_burst_level) {
        burst_level_label->setText(QString("%1 IRE").arg(metrics.burst_level, 0, 'f', 2));
    } else {
        burst_level_label->setText("N/A");
    }
    
    if (metrics.has_quality_score) {
        quality_score_label->setText(QString("%1").arg(metrics.quality_score, 0, 'f', 3));
    } else {
        quality_score_label->setText("N/A");
    }
    
    if (metrics.has_dropout_count) {
        dropout_count_label->setText(QString::number(metrics.dropout_count));
    } else {
        dropout_count_label->setText("N/A");
    }
}

void QualityMetricsDialog::updateFrameAverageLabels(const FieldMetrics& field1, const FieldMetrics& field2)
{
    // Average white SNR
    if (field1.has_white_snr && field2.has_white_snr) {
        double avg = (field1.white_snr + field2.white_snr) / 2.0;
        frame_white_snr_label_->setText(QString("%1 dB").arg(avg, 0, 'f', 2));
    } else if (field1.has_white_snr) {
        frame_white_snr_label_->setText(QString("%1 dB").arg(field1.white_snr, 0, 'f', 2));
    } else if (field2.has_white_snr) {
        frame_white_snr_label_->setText(QString("%1 dB").arg(field2.white_snr, 0, 'f', 2));
    } else {
        frame_white_snr_label_->setText("N/A");
    }
    
    // Average black PSNR
    if (field1.has_black_psnr && field2.has_black_psnr) {
        double avg = (field1.black_psnr + field2.black_psnr) / 2.0;
        frame_black_psnr_label_->setText(QString("%1 dB").arg(avg, 0, 'f', 2));
    } else if (field1.has_black_psnr) {
        frame_black_psnr_label_->setText(QString("%1 dB").arg(field1.black_psnr, 0, 'f', 2));
    } else if (field2.has_black_psnr) {
        frame_black_psnr_label_->setText(QString("%1 dB").arg(field2.black_psnr, 0, 'f', 2));
    } else {
        frame_black_psnr_label_->setText("N/A");
    }
    
    // Average burst level
    if (field1.has_burst_level && field2.has_burst_level) {
        double avg = (field1.burst_level + field2.burst_level) / 2.0;
        frame_burst_level_label_->setText(QString("%1 IRE").arg(avg, 0, 'f', 2));
    } else if (field1.has_burst_level) {
        frame_burst_level_label_->setText(QString("%1 IRE").arg(field1.burst_level, 0, 'f', 2));
    } else if (field2.has_burst_level) {
        frame_burst_level_label_->setText(QString("%1 IRE").arg(field2.burst_level, 0, 'f', 2));
    } else {
        frame_burst_level_label_->setText("N/A");
    }
    
    // Average quality score
    if (field1.has_quality_score && field2.has_quality_score) {
        double avg = (field1.quality_score + field2.quality_score) / 2.0;
        frame_quality_score_label_->setText(QString("%1").arg(avg, 0, 'f', 3));
    } else if (field1.has_quality_score) {
        frame_quality_score_label_->setText(QString("%1").arg(field1.quality_score, 0, 'f', 3));
    } else if (field2.has_quality_score) {
        frame_quality_score_label_->setText(QString("%1").arg(field2.quality_score, 0, 'f', 3));
    } else {
        frame_quality_score_label_->setText("N/A");
    }
    
    // Total dropout count
    size_t total = 0;
    bool has_data = false;
    if (field1.has_dropout_count) {
        total += field1.dropout_count;
        has_data = true;
    }
    if (field2.has_dropout_count) {
        total += field2.dropout_count;
        has_data = true;
    }
    if (has_data) {
        frame_dropout_count_label_->setText(QString::number(total));
    } else {
        frame_dropout_count_label_->setText("N/A");
    }
}





void QualityMetricsDialog::clearMetrics()
{
    // Reset all labels to N/A
    field1_white_snr_label_->setText("N/A");
    field1_black_psnr_label_->setText("N/A");
    field1_burst_level_label_->setText("N/A");
    field1_quality_score_label_->setText("N/A");
    field1_dropout_count_label_->setText("N/A");
    
    field2_white_snr_label_->setText("N/A");
    field2_black_psnr_label_->setText("N/A");
    field2_burst_level_label_->setText("N/A");
    field2_quality_score_label_->setText("N/A");
    field2_dropout_count_label_->setText("N/A");
    
    frame_white_snr_label_->setText("N/A");
    frame_black_psnr_label_->setText("N/A");
    frame_burst_level_label_->setText("N/A");
    frame_quality_score_label_->setText("N/A");
    frame_dropout_count_label_->setText("N/A");
}



void QualityMetricsDialog::updateMetricsFromContext(
    orc::FieldID field_id,
    const orc::ObservationContext& obs_context)
{
    showing_frame_mode_ = false;
    field1_group_->show();
    field2_group_->hide();
    frame_group_->hide();
    field1_group_->setTitle("Field");
    
    FieldMetrics metrics = extractMetricsFromContext(field_id, obs_context);
    updateFieldLabels(metrics, true);
}

void QualityMetricsDialog::updateMetricsForFrameFromContext(
    orc::FieldID field1_id,
    orc::FieldID field2_id,
    const orc::ObservationContext& obs_context)
{
    showing_frame_mode_ = true;
    field1_group_->show();
    field2_group_->show();
    frame_group_->show();
    field1_group_->setTitle("Field 1");
    
    FieldMetrics field1_metrics = extractMetricsFromContext(field1_id, obs_context);
    FieldMetrics field2_metrics = extractMetricsFromContext(field2_id, obs_context);
    
    updateFieldLabels(field1_metrics, true);
    updateFieldLabels(field2_metrics, false);
    updateFrameAverageLabels(field1_metrics, field2_metrics);
}

QualityMetricsDialog::FieldMetrics QualityMetricsDialog::extractMetricsFromContext(
    orc::FieldID field_id,
    const orc::ObservationContext& obs_context)
{
    FieldMetrics metrics;
    
    ORC_LOG_DEBUG("QualityMetricsDialog: Extracting metrics from context for field {}", field_id.value());
    
    // Extract disc quality metrics from observation context
    auto quality_score_opt = obs_context.get(field_id, "disc_quality", "quality_score");
    if (quality_score_opt) {
        metrics.quality_score = std::get<double>(*quality_score_opt);
        metrics.has_quality_score = true;
        ORC_LOG_DEBUG("QualityMetricsDialog: Found quality_score = {}", metrics.quality_score);
    } else {
        ORC_LOG_DEBUG("QualityMetricsDialog: No quality_score found in context");
    }
    
    auto dropout_count_opt = obs_context.get(field_id, "disc_quality", "dropout_count");
    if (dropout_count_opt) {
        metrics.dropout_count = std::get<int32_t>(*dropout_count_opt);
        metrics.has_dropout_count = true;
        ORC_LOG_DEBUG("QualityMetricsDialog: Found dropout_count = {}", metrics.dropout_count);
    } else {
        ORC_LOG_DEBUG("QualityMetricsDialog: No dropout_count found in context");
    }
    
    // Extract burst level from observation context
    auto burst_level_opt = obs_context.get(field_id, "burst_level", "median_burst_ire");
    if (burst_level_opt) {
        metrics.burst_level = std::get<double>(*burst_level_opt);
        metrics.has_burst_level = true;
        ORC_LOG_DEBUG("QualityMetricsDialog: Found burst_level = {}", metrics.burst_level);
    } else {
        ORC_LOG_DEBUG("QualityMetricsDialog: No burst_level found in context");
    }
    
    // Extract white SNR from observation context
    auto white_snr_opt = obs_context.get(field_id, "white_snr", "snr_db");
    if (white_snr_opt) {
        metrics.white_snr = std::get<double>(*white_snr_opt);
        metrics.has_white_snr = true;
        ORC_LOG_DEBUG("QualityMetricsDialog: Found white_snr = {}", metrics.white_snr);
    } else {
        ORC_LOG_DEBUG("QualityMetricsDialog: No white_snr found in context");
    }
    
    // Extract black PSNR from observation context
    auto black_psnr_opt = obs_context.get(field_id, "black_psnr", "psnr_db");
    if (black_psnr_opt) {
        metrics.black_psnr = std::get<double>(*black_psnr_opt);
        metrics.has_black_psnr = true;
        ORC_LOG_DEBUG("QualityMetricsDialog: Found black_psnr = {}", metrics.black_psnr);
    } else {
        ORC_LOG_DEBUG("QualityMetricsDialog: No black_psnr found in context");
    }
    
    return metrics;
}
