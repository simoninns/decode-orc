/*
 * File:        linescopedialog.cpp
 * Module:      orc-gui
 * Purpose:     Line scope dialog for viewing line samples
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "linescopedialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <cmath>

LineScopeDialog::LineScopeDialog(QWidget *parent)
    : QDialog(parent)
    , line_series_(nullptr)
    , current_field_index_(0)
    , current_line_number_(0)
    , current_sample_x_(0)
    , original_sample_x_(0)
    , preview_image_width_(0)
    , sample_marker_(nullptr)
{
    setupUI();
    setWindowTitle("Line Scope");
    
    // Use Qt::Window flag to allow independent positioning
    setWindowFlags(Qt::Window);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size
    resize(900, 500);
}

LineScopeDialog::~LineScopeDialog() = default;

void LineScopeDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Plot widget
    plot_widget_ = new PlotWidget(this);
    plot_widget_->setAxisTitle(Qt::Horizontal, "Sample Position");
    plot_widget_->setAxisTitle(Qt::Vertical, "mV (millivolts)");
    plot_widget_->setAxisRange(Qt::Vertical, -200, 1000);  // Approximate mV range
    plot_widget_->setYAxisIntegerLabels(false);
    plot_widget_->setGridEnabled(true);
    plot_widget_->setLegendEnabled(false);
    plot_widget_->setZoomEnabled(true);
    plot_widget_->setPanEnabled(true);
    
    mainLayout->addWidget(plot_widget_, 1);
    
    // Add navigation controls in a vertical column
    auto* controlLayout = new QVBoxLayout();
    
    line_up_button_ = new QPushButton("↑ Up", this);
    line_up_button_->setToolTip("Move to previous line");
    line_up_button_->setAutoRepeat(true);
    line_up_button_->setAutoRepeatDelay(500);  // 500ms initial delay
    line_up_button_->setAutoRepeatInterval(100);  // 100ms repeat interval
    connect(line_up_button_, &QPushButton::clicked, this, &LineScopeDialog::onLineUp);
    controlLayout->addWidget(line_up_button_);
    
    line_down_button_ = new QPushButton("↓ Down", this);
    line_down_button_->setToolTip("Move to next line");
    line_down_button_->setAutoRepeat(true);
    line_down_button_->setAutoRepeatDelay(500);  // 500ms initial delay
    line_down_button_->setAutoRepeatInterval(100);  // 100ms repeat interval
    connect(line_down_button_, &QPushButton::clicked, this, &LineScopeDialog::onLineDown);
    controlLayout->addWidget(line_down_button_);
    
    // Sample info display
    sample_info_label_ = new QLabel(this);
    sample_info_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont monoFont("Monospace");
    monoFont.setStyleHint(QFont::TypeWriter);
    sample_info_label_->setFont(monoFont);
    
    // Center the button column and add sample info to the right
    auto* buttonContainer = new QHBoxLayout();
    buttonContainer->addStretch();
    buttonContainer->addLayout(controlLayout);
    buttonContainer->addSpacing(20);
    buttonContainer->addWidget(sample_info_label_);
    buttonContainer->addStretch();
    
    mainLayout->addLayout(buttonContainer);
    
    // Add series for line data
    line_series_ = plot_widget_->addSeries("Line Samples");
    
    // Connect plot click to update marker
    connect(plot_widget_, &PlotWidget::plotClicked, this, &LineScopeDialog::onPlotClicked);
    
    // Connect plot drag for continuous marker updates
    connect(plot_widget_, &PlotWidget::plotDragged, this, &LineScopeDialog::onPlotClicked);
}

void LineScopeDialog::setLineSamples(const QString& node_id, uint64_t field_index, int line_number, int sample_x, 
                                      const std::vector<uint16_t>& samples,
                                      const std::optional<orc::VideoParameters>& video_params,
                                      int preview_image_width, int original_sample_x)
{
    // Store current line info for navigation
    current_node_id_ = node_id;
    current_field_index_ = field_index;
    current_line_number_ = line_number;
    current_sample_x_ = sample_x;  // Mapped field-space coordinate
    original_sample_x_ = original_sample_x;  // Original preview-space coordinate
    preview_image_width_ = preview_image_width;
    current_samples_ = samples;  // Store samples for later updates
    current_video_params_ = video_params;  // Store video params for IRE calculations
    
    // Update window title to show stage (node_id), field, and line
    setWindowTitle(QString("Line Scope - Stage: %1 - Field %2, Line %3")
                   .arg(node_id)
                   .arg(field_index)
                   .arg(line_number));
    
    // Handle empty samples gracefully
    if (samples.empty()) {
        // Clear everything and show "No data available" message
        plot_widget_->showNoDataMessage("No data available for this line");
        
        // The series was deleted by showNoDataMessage, null it out
        line_series_ = nullptr;
        
        // Clear sample marker reference
        sample_marker_ = nullptr;
        
        // Clear sample info label
        sample_info_label_->setText("");
        
        // Disable navigation buttons when no data is available
        line_up_button_->setEnabled(false);
        line_down_button_->setEnabled(false);
        
        return;
    }
    
    // Re-enable navigation buttons when we have data
    line_up_button_->setEnabled(true);
    line_down_button_->setEnabled(true);
    
    // Recreate series if it was deleted (e.g., by showNoDataMessage)
    if (!line_series_) {
        line_series_ = plot_widget_->addSeries("Line Samples");
    }
    
    // Clear any "no data" message that might be showing
    plot_widget_->clearNoDataMessage();
    
    // Convert samples to plot points in millivolts
    QVector<QPointF> points;
    points.reserve(samples.size());
    
    // Determine mV conversion factor based on video system
    double ire_to_mv = 7.0;  // Default to PAL
    if (video_params.has_value()) {
        const auto& vp = video_params.value();
        if (vp.system == orc::VideoSystem::NTSC || vp.system == orc::VideoSystem::PAL_M) {
            ire_to_mv = 7.143;  // NTSC uses 7.143 mV/IRE
        }
    }
    
    for (size_t i = 0; i < samples.size(); ++i) {
        double mv_value = static_cast<double>(samples[i]);
        
        // Convert to mV via IRE if we have video parameters
        if (video_params.has_value()) {
            const auto& vp = video_params.value();
            if (vp.black_16b_ire >= 0 && vp.white_16b_ire >= 0) {
                // First convert 16-bit value to IRE
                double ire = (mv_value - vp.black_16b_ire) * 100.0 / (vp.white_16b_ire - vp.black_16b_ire);
                // Then convert IRE to mV
                mv_value = ire * ire_to_mv;
            }
        }
        
        points.append(QPointF(static_cast<double>(i), mv_value));
    }
    
    // Determine tick intervals
    double mv_tick_step = 100.0;  // 100 mV intervals
    double ire_tick_step = 20.0;  // 20 IRE intervals
    
    // Calculate Y-axis range to align with tick steps
    double min_mv, max_mv, min_ire, max_ire;
    
    if (video_params.has_value()) {
        const auto& vp = video_params.value();
        if (vp.black_16b_ire >= 0 && vp.white_16b_ire >= 0) {
            // Convert 16-bit extremes (0 and 65535) to mV via IRE
            double raw_min_ire = (0.0 - vp.black_16b_ire) * 100.0 / (vp.white_16b_ire - vp.black_16b_ire);
            double raw_max_ire = (65535.0 - vp.black_16b_ire) * 100.0 / (vp.white_16b_ire - vp.black_16b_ire);
            double raw_min_mv = raw_min_ire * ire_to_mv;
            double raw_max_mv = raw_max_ire * ire_to_mv;
            
            // Find the range of tick marks that covers the data, anchored at 0
            // For minimum: find the lowest tick that is <= raw_min_mv
            min_mv = std::floor(raw_min_mv / mv_tick_step) * mv_tick_step;
            // For maximum: find the highest tick that is >= raw_max_mv
            max_mv = std::ceil(raw_max_mv / mv_tick_step) * mv_tick_step;
            
            // But don't extend beyond the actual data range
            // The ticks will still be at nice intervals starting from 0
            if (min_mv < raw_min_mv) {
                min_mv = raw_min_mv;
            }
            if (max_mv > raw_max_mv) {
                max_mv = raw_max_mv;
            }
            
            // Calculate corresponding IRE range
            min_ire = min_mv / ire_to_mv;
            max_ire = max_mv / ire_to_mv;
        } else {
            // Defaults when no video params
            min_mv = -200;
            max_mv = 1000;
            min_ire = min_mv / ire_to_mv;
            max_ire = max_mv / ire_to_mv;
        }
    } else {
        min_mv = -200;
        max_mv = 1000;
        min_ire = -28.6;
        max_ire = 142.9;
    }
    
    // Set appropriate color based on theme
    QColor line_color;
    if (PlotWidget::isDarkTheme()) {
        // Use bright color for dark theme
        line_color = QColor(100, 200, 255);  // Light blue
    } else {
        // Use darker color for light theme
        line_color = QColor(0, 100, 200);  // Dark blue
    }
    line_series_->setPen(QPen(line_color, 1));
    
    // Update the plot with calculated ranges based on 16-bit sample range
    line_series_->setData(points);
    plot_widget_->setAxisRange(Qt::Horizontal, 0, static_cast<double>(samples.size() - 1));
    plot_widget_->setAxisRange(Qt::Vertical, min_mv, max_mv);
    plot_widget_->setAxisAutoScale(Qt::Horizontal, false);
    plot_widget_->setAxisAutoScale(Qt::Vertical, false);
    
    // Set custom tick steps with origin at 0
    plot_widget_->setAxisTickStep(Qt::Vertical, mv_tick_step, 0.0);
    
    // Configure secondary Y-axis to show IRE values
    if (video_params.has_value()) {
        const auto& vp = video_params.value();
        if (vp.black_16b_ire >= 0 && vp.white_16b_ire >= 0) {
            plot_widget_->setSecondaryYAxisEnabled(true);
            plot_widget_->setSecondaryYAxisTitle("IRE");
            plot_widget_->setSecondaryYAxisRange(min_ire, max_ire);
            plot_widget_->setSecondaryYAxisTickStep(ire_tick_step, 0.0);
        } else {
            plot_widget_->setSecondaryYAxisEnabled(false);
        }
    } else {
        plot_widget_->setSecondaryYAxisEnabled(false);
    }
    
    // Clear existing markers
    plot_widget_->clearMarkers();
    
    // Add region markers if we have video parameters
    if (video_params.has_value()) {
        const auto& vp = video_params.value();
        
        // Color burst region (cyan)
        if (vp.colour_burst_start >= 0 && vp.colour_burst_end >= 0) {
            auto* cb_start = plot_widget_->addMarker();
            cb_start->setStyle(PlotMarker::VLine);
            cb_start->setPosition(QPointF(static_cast<double>(vp.colour_burst_start), 0));
            cb_start->setPen(QPen(Qt::cyan, 1, Qt::DashLine));
            
            auto* cb_end = plot_widget_->addMarker();
            cb_end->setStyle(PlotMarker::VLine);
            cb_end->setPosition(QPointF(static_cast<double>(vp.colour_burst_end), 0));
            cb_end->setPen(QPen(Qt::cyan, 1, Qt::DashLine));
        }
        
        // Active video region (yellow)
        if (vp.active_video_start >= 0 && vp.active_video_end >= 0) {
            auto* av_start = plot_widget_->addMarker();
            av_start->setStyle(PlotMarker::VLine);
            av_start->setPosition(QPointF(static_cast<double>(vp.active_video_start), 0));
            av_start->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
            
            auto* av_end = plot_widget_->addMarker();
            av_end->setStyle(PlotMarker::VLine);
            av_end->setPosition(QPointF(static_cast<double>(vp.active_video_end), 0));
            av_end->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        }
        
        // IRE level markers (horizontal lines) in mV
        // 0 IRE (black level) - gray
        // 100 IRE (white level) - white/light gray
        if (vp.black_16b_ire >= 0 && vp.white_16b_ire >= 0) {
            auto* ire0 = plot_widget_->addMarker();
            ire0->setStyle(PlotMarker::HLine);
            ire0->setPosition(QPointF(0, 0.0));  // 0 IRE = 0 mV
            ire0->setPen(QPen(Qt::gray, 1, Qt::DashLine));
            
            auto* ire100 = plot_widget_->addMarker();
            ire100->setStyle(PlotMarker::HLine);
            ire100->setPosition(QPointF(0, 100.0 * ire_to_mv));  // 100 IRE in mV
            ire100->setPen(QPen(Qt::lightGray, 1, Qt::DashLine));
        }
    }
    
    // Add click position marker (green)
    updateSampleMarker(sample_x);
    
    plot_widget_->replot();
}

void LineScopeDialog::updateSampleMarker(int sample_x)
{
    if (current_samples_.empty()) {
        return;
    }
    
    // Remove existing sample marker if present
    if (sample_marker_) {
        plot_widget_->removeMarker(sample_marker_);
        sample_marker_ = nullptr;
    }
    
    // Add new click position marker (green)
    if (sample_x >= 0 && sample_x < static_cast<int>(current_samples_.size())) {
        current_sample_x_ = sample_x;
        
        sample_marker_ = plot_widget_->addMarker();
        sample_marker_->setStyle(PlotMarker::VLine);
        sample_marker_->setPosition(QPointF(static_cast<double>(sample_x), 0));
        sample_marker_->setPen(QPen(Qt::green, 2));
        
        // Update sample info display
        uint16_t sample_value = current_samples_[sample_x];
        
        // Determine mV conversion factor based on video system
        double ire_to_mv = 7.0;  // Default to PAL
        if (current_video_params_.has_value()) {
            const auto& vp = current_video_params_.value();
            if (vp.system == orc::VideoSystem::NTSC || vp.system == orc::VideoSystem::PAL_M) {
                ire_to_mv = 7.143;  // NTSC uses 7.143 mV/IRE
            }
        }
        
        QString info_text = QString("Sample: %1").arg(sample_x);
        
        // Add mV and IRE if we have video parameters
        if (current_video_params_.has_value()) {
            const auto& vp = current_video_params_.value();
            if (vp.black_16b_ire >= 0 && vp.white_16b_ire >= 0) {
                double ire = (static_cast<double>(sample_value) - vp.black_16b_ire) * 100.0 / (vp.white_16b_ire - vp.black_16b_ire);
                double mv = ire * ire_to_mv;
                info_text += QString("\nmV: %1").arg(mv, 0, 'f', 1);
                info_text += QString("\nIRE: %1").arg(ire, 0, 'f', 1);
            }
        } else {
            // If no video parameters, show raw 16-bit value
            info_text += QString("\n16-bit: %1").arg(sample_value);
        }
        
        sample_info_label_->setText(info_text);
    } else {
        sample_info_label_->setText("");
    }
    
    plot_widget_->replot();
}

void LineScopeDialog::onPlotClicked(const QPointF &dataPoint)
{
    // Round X coordinate to nearest integer sample position
    int new_sample_x = qRound(dataPoint.x());
    
    // Clamp to valid range
    new_sample_x = qBound(0, new_sample_x, static_cast<int>(current_samples_.size()) - 1);
    
    // Update marker and info
    updateSampleMarker(new_sample_x);
    
    // Emit signal to update cross-hairs in preview
    emit sampleMarkerMoved(new_sample_x);
}

void LineScopeDialog::onLineUp()
{
    // Safety check: don't navigate if no samples available
    if (current_samples_.empty()) {
        return;
    }
    
    // Request previous line (direction = -1)
    // Use the original preview-space coordinate to avoid rounding errors
    emit lineNavigationRequested(-1, current_field_index_, current_line_number_, original_sample_x_, preview_image_width_);
}

void LineScopeDialog::onLineDown()
{
    // Safety check: don't navigate if no samples available
    if (current_samples_.empty()) {
        return;
    }
    
    // Request next line (direction = +1)
    // Use the original preview-space coordinate to avoid rounding errors
    emit lineNavigationRequested(+1, current_field_index_, current_line_number_, original_sample_x_, preview_image_width_);
}
