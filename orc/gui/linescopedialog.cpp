/*
 * File:        linescopedialog.cpp
 * Module:      orc-gui
 * Purpose:     Line scope dialog for viewing line samples
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "linescopedialog.h"
#include "logging.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHideEvent>
#include <cmath>

LineScopeDialog::LineScopeDialog(QWidget *parent)
    : QDialog(parent)
    , line_series_(nullptr)
    , y_series_(nullptr)
    , c_series_(nullptr)
    , current_field_index_(0)
    , current_line_number_(0)
    , current_sample_x_(0)
    , original_sample_x_(0)
    , preview_image_width_(0)
    , sample_marker_(nullptr)
    , is_yc_source_(false)
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

void LineScopeDialog::hideEvent(QHideEvent* event)
{
    QDialog::hideEvent(event);
    emit dialogClosed();
}

void LineScopeDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Plot widget
    plot_widget_ = new PlotWidget(this);
    plot_widget_->setAxisTitle(Qt::Horizontal, "Time (µs)");
    plot_widget_->setAxisTitle(Qt::Vertical, "mV (millivolts)");
    plot_widget_->setAxisRange(Qt::Vertical, -200, 1000);  // Approximate mV range
    plot_widget_->setYAxisIntegerLabels(false);
    plot_widget_->setGridEnabled(true);
    plot_widget_->setLegendEnabled(true);  // Enable legend for YC mode
    plot_widget_->setZoomEnabled(true);
    plot_widget_->setPanEnabled(true);
    
    mainLayout->addWidget(plot_widget_, 1);
    
    // Add navigation controls and channel selector in a horizontal row
    auto* controlRow = new QHBoxLayout();
    
    // Left section: Channel selector for YC sources
    channel_selector_label_ = new QLabel("Channel:");
    channel_selector_ = new QComboBox(this);
    channel_selector_->addItem("Luma (Y)", 0);
    channel_selector_->addItem("Chroma (C)", 1);
    channel_selector_->addItem("Both (Y+C)", 2);
    channel_selector_->setCurrentIndex(2);  // Default to Both
    channel_selector_->setVisible(false);  // Hidden by default, shown for YC sources
    connect(channel_selector_, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &LineScopeDialog::onChannelSelectionChanged);
    controlRow->addWidget(channel_selector_label_);
    controlRow->addWidget(channel_selector_);
    channel_selector_label_->setVisible(false);
    
    controlRow->addStretch();
    
    // Center section: Navigation buttons
    line_up_button_ = new QPushButton("↑ Up", this);
    line_up_button_->setToolTip("Move to previous line");
    line_up_button_->setAutoRepeat(true);
    line_up_button_->setAutoRepeatDelay(500);  // 500ms initial delay
    line_up_button_->setAutoRepeatInterval(100);  // 100ms repeat interval
    connect(line_up_button_, &QPushButton::clicked, this, &LineScopeDialog::onLineUp);
    controlRow->addWidget(line_up_button_);
    
    line_down_button_ = new QPushButton("↓ Down", this);
    line_down_button_->setToolTip("Move to next line");
    line_down_button_->setAutoRepeat(true);
    line_down_button_->setAutoRepeatDelay(500);  // 500ms initial delay
    line_down_button_->setAutoRepeatInterval(100);  // 100ms repeat interval
    connect(line_down_button_, &QPushButton::clicked, this, &LineScopeDialog::onLineDown);
    controlRow->addWidget(line_down_button_);
    
    controlRow->addSpacing(20);
    
    // Right section: Sample info display
    sample_info_label_ = new QLabel(this);
    sample_info_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont monoFont("Monospace");
    monoFont.setStyleHint(QFont::TypeWriter);
    sample_info_label_->setFont(monoFont);
    controlRow->addWidget(sample_info_label_);
    
    controlRow->addStretch();
    
    mainLayout->addLayout(controlRow);
    
    // Add series for line data
    line_series_ = plot_widget_->addSeries("Composite");
    
    // Connect plot click to update marker
    connect(plot_widget_, &PlotWidget::plotClicked, this, &LineScopeDialog::onPlotClicked);
    
    // Connect plot drag for continuous marker updates
    connect(plot_widget_, &PlotWidget::plotDragged, this, &LineScopeDialog::onPlotClicked);
}

void LineScopeDialog::setLineSamples(const QString& node_id, uint64_t field_index, int line_number, int sample_x, 
                                      const std::vector<uint16_t>& samples,
                                      const std::optional<orc::presenters::VideoParametersView>& video_params,
                                      int preview_image_width, int original_sample_x, int original_image_y,
                                      orc::PreviewOutputType preview_mode,
                                      const std::vector<uint16_t>& y_samples,
                                      const std::vector<uint16_t>& c_samples)
{
    // Block signals during setup to prevent premature updatePlotData() calls
    const QSignalBlocker blocker(channel_selector_);
    
    // Store current line info for navigation
    // Note: line_number parameter is 1-based (for display), convert to 0-based for internal use
    current_node_id_ = node_id;
    current_field_index_ = field_index;
    current_line_number_ = line_number - 1;  // Convert 1-based to 0-based for internal storage
    current_sample_x_ = sample_x;  // Mapped field-space coordinate
    original_sample_x_ = original_sample_x;  // Original preview-space X coordinate
    original_image_y_ = original_image_y;    // Original preview-space Y coordinate
    preview_image_width_ = preview_image_width;
    preview_mode_ = preview_mode;
    current_samples_ = samples;  // Store samples for later updates
    current_y_samples_ = y_samples;
    current_c_samples_ = c_samples;
    current_video_params_ = video_params;  // Store video params for IRE calculations
    
    // Determine if this is a YC source
    is_yc_source_ = !y_samples.empty() && !c_samples.empty();
    
    // Show/hide channel selector based on source type
    channel_selector_->setVisible(is_yc_source_);
    channel_selector_label_->setVisible(is_yc_source_);
    
    // For YC sources, default to "Both (Y+C)" mode on first display only
    static bool first_yc_display = true;
    if (is_yc_source_ && first_yc_display) {
        channel_selector_->setCurrentIndex(2);  // Both (Y+C) at index 2
        first_yc_display = false;
    }
    // Otherwise keep the user's current selection
    
    // Update window title based on preview mode
    QString system_suffix;
    orc::presenters::VideoSystem presenter_system = orc::presenters::VideoSystem::Unknown;
    if (video_params.has_value()) {
        const auto& vp = video_params.value();
        presenter_system = vp.system;
        if (vp.system == orc::presenters::VideoSystem::NTSC) {
            system_suffix = " (NTSC)";
        } else if (vp.system == orc::presenters::VideoSystem::PAL) {
            system_suffix = " (PAL)";
        } else if (vp.system == orc::presenters::VideoSystem::PAL_M) {
            system_suffix = " (PAL-M)";
        }
    }
    QString yc_suffix = is_yc_source_ ? " (YC Source)" : "";
    
    // Build title based on preview mode
    // Note: current_line_number_ is stored as 0-based, convert to 1-based for display
    int line_number_1based = current_line_number_ + 1;
    QString title;
    if (preview_mode == orc::PreviewOutputType::Frame || 
        preview_mode == orc::PreviewOutputType::Frame_Reversed ||
        preview_mode == orc::PreviewOutputType::Split) {
        // Frame or Split mode: show frame number and frame line number
        if (presenter_system != orc::presenters::VideoSystem::Unknown) {
            auto frame_coords = orc::presenters::fieldToFrameCoordinates(presenter_system, field_index, line_number_1based);
            if (frame_coords.has_value()) {
                title = QString("Line Scope%1 - Stage: %2 - Frame %3, Line %4%5")
                    .arg(system_suffix)
                    .arg(node_id)
                    .arg(frame_coords->frame_number)
                    .arg(frame_coords->frame_line_number)
                    .arg(yc_suffix);
            } else {
                // Fallback if conversion fails
                title = QString("Line Scope%1 - Stage: %2 - Field %3, Line %4%5")
                    .arg(system_suffix)
                    .arg(node_id)
                    .arg(field_index + 1)  // Display as 1-based
                    .arg(line_number_1based)
                    .arg(yc_suffix);
            }
        } else {
            // No video params, fallback to field numbering
            title = QString("Line Scope%1 - Stage: %2 - Field %3, Line %4%5")
                .arg(system_suffix)
                .arg(node_id)
                .arg(field_index + 1)  // Display as 1-based
                .arg(line_number_1based)
                .arg(yc_suffix);
        }
    } else {
        // Field mode: show field number and field line number
        title = QString("Line Scope%1 - Stage: %2 - Field %3, Line %4%5")
            .arg(system_suffix)
            .arg(node_id)
            .arg(field_index + 1)  // Display as 1-based
            .arg(line_number_1based)
            .arg(yc_suffix);
    }
    setWindowTitle(title);
    
    // Handle empty samples gracefully
    if (samples.empty() && !is_yc_source_) {
        // Clear everything and show "No data available" message
        plot_widget_->showNoDataMessage("No data available for this line");
        
        // The series was deleted by showNoDataMessage, null it out
        line_series_ = nullptr;
        y_series_ = nullptr;
        c_series_ = nullptr;
        
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
    
    // Clear any "no data" message that might be showing
    plot_widget_->clearNoDataMessage();
    
    // Update plot data based on current channel selection
    updatePlotData();
    
    // Add click position marker (green)
    updateSampleMarker(sample_x);
    
    plot_widget_->replot();
}

void LineScopeDialog::updatePlotData()
{
    // Guard against being called before data is set
    if (!is_yc_source_) {
        if (current_samples_.empty()) {
            return;  // No data to plot yet
        }
    } else {
        // For YC sources, need at least one of Y or C samples
        if (current_y_samples_.empty() && current_c_samples_.empty()) {
            return;  // No YC data to plot yet
        }
    }
    
    // Determine which samples to display based on channel selection
    // New indices: 0=Y, 1=C, 2=Both
    int channel_mode = is_yc_source_ ? channel_selector_->currentIndex() : -1;
    
    // Get the samples to display
    const std::vector<uint16_t>* display_samples = nullptr;
    if (is_yc_source_) {
        if (channel_mode == 0 && !current_y_samples_.empty()) {  // Luma (Y) only
            display_samples = &current_y_samples_;
        } else if (channel_mode == 1 && !current_c_samples_.empty()) {  // Chroma (C) only
            display_samples = &current_c_samples_;
        } else if (channel_mode == 2) {  // Both (Y+C)
            // Will handle separately below - don't set display_samples
        }
        // If none of the above conditions are met, display_samples stays nullptr
    } else {
        if (!current_samples_.empty()) {
            display_samples = &current_samples_;
        }
    }
    
    // Convert samples to plot points in millivolts with X-axis in microseconds
    auto convertSamplesToPoints = [this](const std::vector<uint16_t>& samples) -> QVector<QPointF> {
        if (samples.empty()) {
            return QVector<QPointF>();  // Return empty if no samples
        }
        
        QVector<QPointF> points;
        points.reserve(samples.size());
        
        // Determine mV conversion factor based on video system
        double ire_to_mv = 7.0;  // Default to PAL
        if (current_video_params_.has_value()) {
            const auto& vp = current_video_params_.value();
            if (vp.system == orc::presenters::VideoSystem::NTSC || vp.system == orc::presenters::VideoSystem::PAL_M) {
                ire_to_mv = 7.143;  // NTSC uses 7.143 mV/IRE
            }
        }
        
        // Calculate microseconds per sample (default to 1.0 if no sample rate available)
        double us_per_sample = 1.0;
        if (current_video_params_.has_value() && current_video_params_->sample_rate > 0) {
            // sample_rate is in Hz (samples per second)
            // Convert to microseconds per sample: (1 / sample_rate) * 1,000,000
            us_per_sample = 1000000.0 / current_video_params_->sample_rate;
        }
        
        for (size_t i = 0; i < samples.size(); ++i) {
            double mv_value = static_cast<double>(samples[i]);
            
            // Convert to mV via IRE if we have video parameters
            if (current_video_params_.has_value()) {
                const auto& vp = current_video_params_.value();
                if (vp.blanking_ire >= 0 && vp.white_ire >= 0) {
                    // Use blanking as reference for 0 IRE, white as 100 IRE
                    double ire = (mv_value - vp.blanking_ire) * 100.0 / (vp.white_ire - vp.blanking_ire);
                    // Then convert IRE to mV
                    mv_value = ire * ire_to_mv;
                } else if (vp.black_ire >= 0 && vp.white_ire >= 0) {
                    // Fallback to black level if blanking is not available
                    double ire = (mv_value - vp.black_ire) * 100.0 / (vp.white_ire - vp.black_ire);
                    mv_value = ire * ire_to_mv;
                }
            }
            
            // X-axis: convert sample position to microseconds
            double time_us = static_cast<double>(i) * us_per_sample;
            points.append(QPointF(time_us, mv_value));
        }
        
        return points;
    };
    
    // Recreate series if needed
    if (is_yc_source_ && channel_mode == 2 && !current_y_samples_.empty() && !current_c_samples_.empty()) {
        // Both Y and C - need two series (this is the only case that needs a legend)
        plot_widget_->setLegendEnabled(true);
        
        if (!y_series_) {
            y_series_ = plot_widget_->addSeries("Luma (Y)");
        }
        if (!c_series_) {
            c_series_ = plot_widget_->addSeries("Chroma (C)");
        }
        // Remove composite series if it exists
        if (line_series_) {
            plot_widget_->removeSeries(line_series_);
            line_series_ = nullptr;
        }
        
        // Convert and set data
        QVector<QPointF> y_points = convertSamplesToPoints(current_y_samples_);
        QVector<QPointF> c_points = convertSamplesToPoints(current_c_samples_);
        
        // Set appropriate colors
        QColor y_color, c_color;
        if (PlotWidget::isDarkTheme()) {
            y_color = QColor(255, 255, 100);  // Bright yellow for Y
            c_color = QColor(100, 150, 255);  // Bright blue for C
        } else {
            y_color = QColor(200, 180, 0);  // Dark yellow for Y
            c_color = QColor(0, 80, 200);  // Dark blue for C
        }
        
        y_series_->setPen(QPen(y_color, 1));
        c_series_->setPen(QPen(c_color, 1));
        
        y_series_->setData(y_points);
        c_series_->setData(c_points);
        
    } else if (display_samples && !display_samples->empty()) {
        // Single channel mode - use line_series_ (no legend needed for single dataset)
        plot_widget_->setLegendEnabled(false);
        
        QString label = "Composite";
        if (is_yc_source_) {
            if (channel_mode == 0) {
                label = "Luma (Y)";
            } else if (channel_mode == 1) {
                label = "Chroma (C)";
            }
        }
        
        if (!line_series_) {
            line_series_ = plot_widget_->addSeries(label);
        } else {
            // Update legend label when switching channels
            line_series_->setTitle(label);
        }
        
        // Remove Y/C series if they exist
        if (y_series_) {
            plot_widget_->removeSeries(y_series_);
            y_series_ = nullptr;
        }
        if (c_series_) {
            plot_widget_->removeSeries(c_series_);
            c_series_ = nullptr;
        }
        
        QVector<QPointF> points = convertSamplesToPoints(*display_samples);
        
        // Set appropriate color based on theme and channel
        QColor line_color;
        if (PlotWidget::isDarkTheme()) {
            if (is_yc_source_ && channel_mode == 0) {
                line_color = QColor(255, 255, 100);  // Bright yellow for Y
            } else if (is_yc_source_ && channel_mode == 1) {
                line_color = QColor(100, 150, 255);  // Bright blue for C
            } else {
                line_color = QColor(100, 200, 255);  // Light blue for composite
            }
        } else {
            if (is_yc_source_ && channel_mode == 0) {
                line_color = QColor(200, 180, 0);  // Dark yellow for Y
            } else if (is_yc_source_ && channel_mode == 1) {
                line_color = QColor(0, 80, 200);  // Dark blue for C
            } else {
                line_color = QColor(0, 100, 200);  // Dark blue for composite
            }
        }
        line_series_->setPen(QPen(line_color, 1));
        line_series_->setData(points);
    } else {
        // No valid data to plot - clear everything and return
        if (line_series_) {
            plot_widget_->removeSeries(line_series_);
            line_series_ = nullptr;
        }
        if (y_series_) {
            plot_widget_->removeSeries(y_series_);
            y_series_ = nullptr;
        }
        if (c_series_) {
            plot_widget_->removeSeries(c_series_);
            c_series_ = nullptr;
        }
        return;  // Nothing to plot
    }
    
    // Determine tick intervals and ranges
    double mv_tick_step = 100.0;  // 100 mV intervals
    double ire_tick_step = 20.0;  // 20 IRE intervals
    double ire_to_mv = 7.0;  // Default to PAL
    
    if (current_video_params_.has_value()) {
        const auto& vp = current_video_params_.value();
        if (vp.system == orc::presenters::VideoSystem::NTSC || vp.system == orc::presenters::VideoSystem::PAL_M) {
            ire_to_mv = 7.143;  // NTSC uses 7.143 mV/IRE
        }
    }
    
    // Calculate Y-axis range
    double min_mv, max_mv, min_ire, max_ire;
    
    if (current_video_params_.has_value()) {
        const auto& vp = current_video_params_.value();
        if (vp.blanking_ire >= 0 && vp.white_ire >= 0) {
            // Convert 16-bit extremes (0 and 65535) to mV via IRE
            // Using blanking as reference for 0 IRE
            double raw_min_ire = (0.0 - vp.blanking_ire) * 100.0 / (vp.white_ire - vp.blanking_ire);
            double raw_max_ire = (65535.0 - vp.blanking_ire) * 100.0 / (vp.white_ire - vp.blanking_ire);
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
        } else if (vp.black_ire >= 0 && vp.white_ire >= 0) {
            // Fallback to black level if blanking not available
            double raw_min_ire = (0.0 - vp.black_ire) * 100.0 / (vp.white_ire - vp.black_ire);
            double raw_max_ire = (65535.0 - vp.black_ire) * 100.0 / (vp.white_ire - vp.black_ire);
            double raw_min_mv = raw_min_ire * ire_to_mv;
            double raw_max_mv = raw_max_ire * ire_to_mv;
            
            min_mv = std::floor(raw_min_mv / mv_tick_step) * mv_tick_step;
            max_mv = std::ceil(raw_max_mv / mv_tick_step) * mv_tick_step;
            
            if (min_mv < raw_min_mv) {
                min_mv = raw_min_mv;
            }
            if (max_mv > raw_max_mv) {
                max_mv = raw_max_mv;
            }
            
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
    
    // Determine sample count for X-axis range
    size_t sample_count = 0;
    if (display_samples) {
        sample_count = display_samples->size();
    }
    if (is_yc_source_ && channel_mode == 2) {
        // Use Y samples size for both mode
        sample_count = current_y_samples_.size();
    }
    
    // Calculate time duration in microseconds for X-axis range
    double us_per_sample = 1.0;
    if (current_video_params_.has_value() && current_video_params_->sample_rate > 0) {
        us_per_sample = 1000000.0 / current_video_params_->sample_rate;
    }
    double max_time_us = static_cast<double>(sample_count - 1) * us_per_sample;
    
    // Update the plot with calculated ranges (X-axis now in microseconds)
    plot_widget_->setAxisRange(Qt::Horizontal, 0, max_time_us);
    plot_widget_->setAxisRange(Qt::Vertical, min_mv, max_mv);
    plot_widget_->setAxisAutoScale(Qt::Horizontal, false);
    plot_widget_->setAxisAutoScale(Qt::Vertical, false);
    
    // Set custom tick steps with origin at 0
    plot_widget_->setAxisTickStep(Qt::Horizontal, 2.0, 0.0);  // 2 µs tick marks
    plot_widget_->setAxisTickStep(Qt::Vertical, mv_tick_step, 0.0);
    
    // Configure secondary Y-axis to show IRE values
    if (current_video_params_.has_value()) {
        const auto& vp = current_video_params_.value();
        if (vp.black_ire >= 0 && vp.white_ire >= 0) {
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
    if (current_video_params_.has_value()) {
        const auto& vp = current_video_params_.value();
        
        // Convert sample positions to microseconds for X-axis
        double us_per_sample = 1.0;
        if (vp.sample_rate > 0) {
            us_per_sample = 1000000.0 / vp.sample_rate;
        }
        
        // Color burst region (cyan)
        if (vp.color_burst_start >= 0 && vp.color_burst_end >= 0) {
            double cb_start_us = static_cast<double>(vp.color_burst_start) * us_per_sample;
            double cb_end_us = static_cast<double>(vp.color_burst_end) * us_per_sample;
            
            auto* cb_start = plot_widget_->addMarker();
            cb_start->setStyle(PlotMarker::VLine);
            cb_start->setPosition(QPointF(cb_start_us, 0));
            cb_start->setPen(QPen(Qt::cyan, 1, Qt::DashLine));
            
            auto* cb_end = plot_widget_->addMarker();
            cb_end->setStyle(PlotMarker::VLine);
            cb_end->setPosition(QPointF(cb_end_us, 0));
            cb_end->setPen(QPen(Qt::cyan, 1, Qt::DashLine));
        }
        
        // Active video region (yellow)
        if (vp.active_video_start >= 0 && vp.active_video_end >= 0) {
            double av_start_us = static_cast<double>(vp.active_video_start) * us_per_sample;
            double av_end_us = static_cast<double>(vp.active_video_end) * us_per_sample;
            
            auto* av_start = plot_widget_->addMarker();
            av_start->setStyle(PlotMarker::VLine);
            av_start->setPosition(QPointF(av_start_us, 0));
            av_start->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
            
            auto* av_end = plot_widget_->addMarker();
            av_end->setStyle(PlotMarker::VLine);
            av_end->setPosition(QPointF(av_end_us, 0));
            av_end->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        }
        
        // IRE level markers (horizontal lines) in mV
        // 0 IRE (blanking level) - dark gray
        // Black level - light gray (if different from blanking)
        // 100 IRE (white level) - light gray
        if (vp.blanking_ire >= 0 && vp.white_ire >= 0) {
            // 0 IRE (blanking) = 0 mV by definition
            auto* ire0 = plot_widget_->addMarker();
            ire0->setStyle(PlotMarker::HLine);
            ire0->setPosition(QPointF(0, 0.0));  // 0 IRE = 0 mV
            ire0->setPen(QPen(Qt::darkGray, 1, Qt::DashLine));
            
            // Black level marker (if different from blanking)
            if (vp.black_ire >= 0 && vp.black_ire != vp.blanking_ire) {
                double black_ire = (static_cast<double>(vp.black_ire) - vp.blanking_ire) * 100.0 / (vp.white_ire - vp.blanking_ire);
                double black_mv = black_ire * ire_to_mv;
                auto* black_marker = plot_widget_->addMarker();
                black_marker->setStyle(PlotMarker::HLine);
                black_marker->setPosition(QPointF(0, black_mv));
                black_marker->setPen(QPen(Qt::gray, 1, Qt::DashDotLine));
            }
            
            // 100 IRE (white level)
            auto* ire100 = plot_widget_->addMarker();
            ire100->setStyle(PlotMarker::HLine);
            ire100->setPosition(QPointF(0, 100.0 * ire_to_mv));  // 100 IRE in mV
            ire100->setPen(QPen(Qt::lightGray, 1, Qt::DashLine));
        } else if (vp.black_ire >= 0 && vp.white_ire >= 0) {
            // Fallback: use black level as reference if blanking not available
            auto* ire0 = plot_widget_->addMarker();
            ire0->setStyle(PlotMarker::HLine);
            ire0->setPosition(QPointF(0, 0.0));  // Reference point
            ire0->setPen(QPen(Qt::darkGray, 1, Qt::DashLine));
            
            auto* ire100 = plot_widget_->addMarker();
            ire100->setStyle(PlotMarker::HLine);
            ire100->setPosition(QPointF(0, 100.0 * ire_to_mv));
            ire100->setPen(QPen(Qt::lightGray, 1, Qt::DashLine));
        }
    }
}

void LineScopeDialog::onChannelSelectionChanged(int /*index*/)
{
    // Guard against being called before data is set
    if (!is_yc_source_) {
        return;  // Channel selector is only used for YC sources
    }
    
    // Redraw plot with new channel selection
    updatePlotData();
    updateSampleMarker(current_sample_x_);
    plot_widget_->replot();
}

void LineScopeDialog::updateSampleMarker(int sample_x)
{
    // Determine which samples to use for getting values
    const std::vector<uint16_t>* samples_for_marker = &current_samples_;
    if (is_yc_source_) {
        int channel_mode = channel_selector_->currentIndex();
        if (channel_mode == 0) {
            samples_for_marker = &current_y_samples_;
        } else if (channel_mode == 1) {
            samples_for_marker = &current_c_samples_;
        } else if (channel_mode == 2) {
            // For "Both" mode, use Y samples
            samples_for_marker = &current_y_samples_;
        }
    }
    
    if (samples_for_marker->empty()) {
        return;
    }
    
    // Remove existing sample marker if present
    if (sample_marker_) {
        plot_widget_->removeMarker(sample_marker_);
        sample_marker_ = nullptr;
    }
    
    // Add new click position marker (green)
    if (sample_x >= 0 && sample_x < static_cast<int>(samples_for_marker->size())) {
        current_sample_x_ = sample_x;
        
        // Calculate time position in microseconds
        double us_per_sample = 1.0;
        if (current_video_params_.has_value() && current_video_params_->sample_rate > 0) {
            us_per_sample = 1000000.0 / current_video_params_->sample_rate;
        }
        double time_us = static_cast<double>(sample_x) * us_per_sample;
        
        sample_marker_ = plot_widget_->addMarker();
        sample_marker_->setStyle(PlotMarker::VLine);
        sample_marker_->setPosition(QPointF(time_us, 0));
        sample_marker_->setPen(QPen(Qt::green, 2));
        
        // Update sample info display
        uint16_t sample_value = (*samples_for_marker)[sample_x];
        
        // Determine mV conversion factor based on video system
        double ire_to_mv = 7.0;  // Default to PAL
        if (current_video_params_.has_value()) {
            const auto& vp = current_video_params_.value();
            if (vp.system == orc::presenters::VideoSystem::NTSC || vp.system == orc::presenters::VideoSystem::PAL_M) {
                ire_to_mv = 7.143;  // NTSC uses 7.143 mV/IRE
            }
        }
        
        QString info_text = QString("Time: %1 µs (Sample: %2)").arg(time_us, 0, 'f', 3).arg(sample_x);
        
        // Add channel info for YC sources in "Both" mode
        if (is_yc_source_ && channel_selector_->currentIndex() == 2) {
            // Show both Y and C values
            if (sample_x < static_cast<int>(current_y_samples_.size()) && 
                sample_x < static_cast<int>(current_c_samples_.size())) {
                uint16_t y_value = current_y_samples_[sample_x];
                uint16_t c_value = current_c_samples_[sample_x];
                
                if (current_video_params_.has_value()) {
                    const auto& vp = current_video_params_.value();
                    if (vp.blanking_ire >= 0 && vp.white_ire >= 0) {
                        double y_ire = (static_cast<double>(y_value) - vp.blanking_ire) * 100.0 / (vp.white_ire - vp.blanking_ire);
                        double c_ire = (static_cast<double>(c_value) - vp.blanking_ire) * 100.0 / (vp.white_ire - vp.blanking_ire);
                        double y_mv = y_ire * ire_to_mv;
                        double c_mv = c_ire * ire_to_mv;
                        
                        info_text += QString("\nY: %1 mV (%2 IRE)").arg(y_mv, 0, 'f', 1).arg(y_ire, 0, 'f', 1);
                        info_text += QString("\nC: %1 mV (%2 IRE)").arg(c_mv, 0, 'f', 1).arg(c_ire, 0, 'f', 1);
                    } else if (vp.black_ire >= 0 && vp.white_ire >= 0) {
                        // Fallback if blanking not available
                        double y_ire = (static_cast<double>(y_value) - vp.black_ire) * 100.0 / (vp.white_ire - vp.black_ire);
                        double c_ire = (static_cast<double>(c_value) - vp.black_ire) * 100.0 / (vp.white_ire - vp.black_ire);
                        double y_mv = y_ire * ire_to_mv;
                        double c_mv = c_ire * ire_to_mv;
                        
                        info_text += QString("\nY: %1 mV (%2 IRE)").arg(y_mv, 0, 'f', 1).arg(y_ire, 0, 'f', 1);
                        info_text += QString("\nC: %1 mV (%2 IRE)").arg(c_mv, 0, 'f', 1).arg(c_ire, 0, 'f', 1);
                    }
                } else {
                    info_text += QString("\nY: %1").arg(y_value);
                    info_text += QString("\nC: %1").arg(c_value);
                }
            }
        } else {
            // Single channel mode - show mV and IRE if we have video parameters
            if (current_video_params_.has_value()) {
                const auto& vp = current_video_params_.value();
                if (vp.blanking_ire >= 0 && vp.white_ire >= 0) {
                    double ire = (static_cast<double>(sample_value) - vp.blanking_ire) * 100.0 / (vp.white_ire - vp.blanking_ire);
                    double mv = ire * ire_to_mv;
                    info_text += QString("\nmV: %1").arg(mv, 0, 'f', 1);
                    info_text += QString("\nIRE: %1").arg(ire, 0, 'f', 1);
                } else if (vp.black_ire >= 0 && vp.white_ire >= 0) {
                    // Fallback if blanking not available
                    double ire = (static_cast<double>(sample_value) - vp.black_ire) * 100.0 / (vp.white_ire - vp.black_ire);
                    double mv = ire * ire_to_mv;
                    info_text += QString("\nmV: %1").arg(mv, 0, 'f', 1);
                    info_text += QString("\nIRE: %1").arg(ire, 0, 'f', 1);
                }
            } else {
                // If no video parameters, show raw 16-bit value
                info_text += QString("\n16-bit: %1").arg(sample_value);
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
    // Convert X coordinate from microseconds to sample position
    double us_per_sample = 1.0;
    if (current_video_params_.has_value() && current_video_params_->sample_rate > 0) {
        us_per_sample = 1000000.0 / current_video_params_->sample_rate;
    }
    
    // dataPoint.x() is in microseconds, convert to sample position
    int new_sample_x = qRound(dataPoint.x() / us_per_sample);
    
    // Determine which samples to use for validation
    const std::vector<uint16_t>* samples_to_check = &current_samples_;
    if (is_yc_source_) {
        int channel_mode = channel_selector_->currentIndex();
        if (channel_mode == 0) {
            samples_to_check = &current_y_samples_;
        } else if (channel_mode == 1) {
            samples_to_check = &current_c_samples_;
        } else if (channel_mode == 2) {
            samples_to_check = &current_y_samples_;
        }
    }
    
    if (samples_to_check->empty()) {
        return;
    }
    
    // Clamp to valid range
    new_sample_x = qBound(0, new_sample_x, static_cast<int>(samples_to_check->size()) - 1);
    
    // Update marker and info
    updateSampleMarker(new_sample_x);
    
    // Emit signal to update cross-hairs in preview
    emit sampleMarkerMoved(new_sample_x);
}

void LineScopeDialog::onLineUp()
{
    // Safety check: allow navigation if composite OR YC samples are available
    bool has_any_samples = !current_samples_.empty() ||
                           (!current_y_samples_.empty() || !current_c_samples_.empty());
    if (!has_any_samples) return;
    
    // Request previous line (direction = -1)
    // Use the original preview-space coordinate to avoid rounding errors
    emit lineNavigationRequested(-1, current_field_index_, current_line_number_, original_sample_x_, preview_image_width_);
}

void LineScopeDialog::onLineDown()
{
    // Safety check: allow navigation if composite OR YC samples are available
    bool has_any_samples = !current_samples_.empty() ||
                           (!current_y_samples_.empty() || !current_c_samples_.empty());
    if (!has_any_samples) return;
    
    // Request next line (direction = +1)
    // Use the original preview-space coordinate to avoid rounding errors
    emit lineNavigationRequested(+1, current_field_index_, current_line_number_, original_sample_x_, preview_image_width_);
}

void LineScopeDialog::refreshSamples()
{
    // Request the current line data to be refreshed using original image coordinates
    // This ensures the same visual position is maintained when frames change
    emit refreshRequested(original_sample_x_, original_image_y_);
}

void LineScopeDialog::refreshSamplesAtCurrentPosition()
{
    // Refresh line samples at the current field/line that this dialog is tracking
    // This is called when the preview frame changes. We need to go through the normal
    // onLineScopeRequested path to properly recalculate image coordinates for the new frame.
    // Using the stored original_sample_x_, original_image_y_ ensures the visual position
    // on screen is maintained while getting fresh samples for the new frame context.
    if (preview_image_width_ > 0) {
        emit refreshRequested(original_sample_x_, original_image_y_);
    }
}
