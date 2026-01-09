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
    plot_widget_->setAxisTitle(Qt::Vertical, "16-bit Sample Value");
    plot_widget_->setAxisRange(Qt::Vertical, 0, 65535);  // 16-bit range
    plot_widget_->setYAxisIntegerLabels(true);
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
    
    // Enable and configure secondary Y-axis if we have video parameters with IRE levels
    if (video_params.has_value()) {
        const auto& vp = video_params.value();
        if (vp.black_16b_ire >= 0 && vp.white_16b_ire >= 0) {
            // Calculate IRE range based on the 16-bit Y-axis range (0-65535)
            // 0 IRE is at black_16b_ire, 100 IRE is at white_16b_ire
            // Formula: IRE = (16bit_value - black_16b_ire) / (white_16b_ire - black_16b_ire) * 100
            
            double ire_per_16bit = 100.0 / (vp.white_16b_ire - vp.black_16b_ire);
            
            // Calculate IRE at 16-bit value 0
            double ire_at_0 = (0 - vp.black_16b_ire) * ire_per_16bit;
            
            // Calculate IRE at 16-bit value 65535
            double ire_at_65535 = (65535 - vp.black_16b_ire) * ire_per_16bit;
            
            plot_widget_->setSecondaryYAxisEnabled(true);
            plot_widget_->setSecondaryYAxisTitle("IRE");
            plot_widget_->setSecondaryYAxisRange(ire_at_0, ire_at_65535);
        } else {
            plot_widget_->setSecondaryYAxisEnabled(false);
        }
    } else {
        plot_widget_->setSecondaryYAxisEnabled(false);
    }
    
    // Convert samples to plot points
    QVector<QPointF> points;
    points.reserve(samples.size());
    
    for (size_t i = 0; i < samples.size(); ++i) {
        points.append(QPointF(static_cast<double>(i), static_cast<double>(samples[i])));
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
    
    // Update the plot
    line_series_->setData(points);
    plot_widget_->setAxisRange(Qt::Horizontal, 0, static_cast<double>(samples.size() - 1));
    plot_widget_->setAxisAutoScale(Qt::Horizontal, false);
    plot_widget_->setAxisAutoScale(Qt::Vertical, false);
    
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
        
        // IRE level markers (horizontal lines)
        // 0 IRE (black level) - gray
        if (vp.black_16b_ire >= 0) {
            auto* ire0 = plot_widget_->addMarker();
            ire0->setStyle(PlotMarker::HLine);
            ire0->setPosition(QPointF(0, static_cast<double>(vp.black_16b_ire)));
            ire0->setPen(QPen(Qt::gray, 1, Qt::DashLine));
        }
        
        // 100 IRE (white level) - white/light gray
        if (vp.white_16b_ire >= 0) {
            auto* ire100 = plot_widget_->addMarker();
            ire100->setStyle(PlotMarker::HLine);
            ire100->setPosition(QPointF(0, static_cast<double>(vp.white_16b_ire)));
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
        QString info_text = QString("Sample: %1\nValue: %2")
            .arg(sample_x)
            .arg(sample_value);
        
        // Add IRE if we have video parameters
        if (current_video_params_.has_value()) {
            const auto& vp = current_video_params_.value();
            if (vp.black_16b_ire >= 0 && vp.white_16b_ire >= 0) {
                double ire = (static_cast<double>(sample_value) - vp.black_16b_ire) * 100.0 / (vp.white_16b_ire - vp.black_16b_ire);
                info_text += QString("\nIRE: %1").arg(ire, 0, 'f', 1);
            }
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
    // Map current_sample_x back to preview-space for navigation
    int nav_sample_x = (current_sample_x_ * preview_image_width_) / current_samples_.size();
    emit lineNavigationRequested(-1, current_field_index_, current_line_number_, nav_sample_x, preview_image_width_);
}

void LineScopeDialog::onLineDown()
{
    // Safety check: don't navigate if no samples available
    if (current_samples_.empty()) {
        return;
    }
    
    // Request next line (direction = +1)
    // Map current_sample_x back to preview-space for navigation
    int nav_sample_x = (current_sample_x_ * preview_image_width_) / current_samples_.size();
    emit lineNavigationRequested(+1, current_field_index_, current_line_number_, nav_sample_x, preview_image_width_);
}
