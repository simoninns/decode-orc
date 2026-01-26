/*
 * File:        fieldtimingwidget.cpp
 * Module:      orc-gui
 * Purpose:     Widget for rendering field timing graphs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "fieldtimingwidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

double FieldTimingWidget::convertSampleToMV(uint16_t sample) const
{
    if (!video_params_.has_value()) {
        // No video params - just return raw sample value scaled down
        return sample / 100.0;  // Scale to reasonable range
    }
    
    const auto& vp = video_params_.value();
    
    // Determine mV conversion factor based on video system
    double ire_to_mv = 7.0;  // Default to PAL
    if (vp.system == orc::presenters::VideoSystem::NTSC || 
        vp.system == orc::presenters::VideoSystem::PAL_M) {
        ire_to_mv = 7.143;  // NTSC uses 7.143 mV/IRE
    }
    
    double mv_value = static_cast<double>(sample);
    
    // Convert to mV via IRE if we have video parameters
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
    
    return mv_value;
}

double FieldTimingWidget::getMVRange(double& min_mv, double& max_mv) const
{
    if (!video_params_.has_value()) {
        min_mv = 0;
        max_mv = 655.35;  // 65535 / 100
        return max_mv - min_mv;
    }
    
    const auto& vp = video_params_.value();
    
    // Determine mV conversion factor
    double ire_to_mv = 7.0;
    if (vp.system == orc::presenters::VideoSystem::NTSC || 
        vp.system == orc::presenters::VideoSystem::PAL_M) {
        ire_to_mv = 7.143;
    }
    
    if (vp.blanking_ire >= 0 && vp.white_ire >= 0) {
        // Convert 16-bit extremes to mV via IRE
        double raw_min_ire = (0.0 - vp.blanking_ire) * 100.0 / (vp.white_ire - vp.blanking_ire);
        double raw_max_ire = (65535.0 - vp.blanking_ire) * 100.0 / (vp.white_ire - vp.blanking_ire);
        min_mv = raw_min_ire * ire_to_mv;
        max_mv = raw_max_ire * ire_to_mv;
    } else if (vp.black_ire >= 0 && vp.white_ire >= 0) {
        double raw_min_ire = (0.0 - vp.black_ire) * 100.0 / (vp.white_ire - vp.black_ire);
        double raw_max_ire = (65535.0 - vp.black_ire) * 100.0 / (vp.white_ire - vp.black_ire);
        min_mv = raw_min_ire * ire_to_mv;
        max_mv = raw_max_ire * ire_to_mv;
    } else {
        min_mv = -200;
        max_mv = 1000;
    }
    
    return max_mv - min_mv;
}

FieldTimingWidget::FieldTimingWidget(QWidget *parent)
    : QWidget(parent)
    , scroll_offset_(0)
    , zoom_factor_(1.0)
{
    // Create scroll bar
    scroll_bar_ = new QScrollBar(Qt::Horizontal, this);
    connect(scroll_bar_, &QScrollBar::valueChanged, [this](int value) {
        scroll_offset_ = value;
        update();
    });
    
    // Set minimum size
    setMinimumSize(600, 400);
    
    // Enable mouse tracking for interactive features
    setMouseTracking(true);
}

void FieldTimingWidget::setFieldData(const std::vector<uint16_t>& samples,
                                    const std::vector<uint16_t>& samples_2,
                                    const std::vector<uint16_t>& y_samples,
                                    const std::vector<uint16_t>& c_samples,
                                    const std::vector<uint16_t>& y_samples_2,
                                    const std::vector<uint16_t>& c_samples_2,
                                    const std::optional<orc::presenters::VideoParametersView>& video_params,
                                    const std::optional<int>& marker_sample)
{
    field1_samples_ = samples;
    field2_samples_ = samples_2;
    y1_samples_ = y_samples;
    c1_samples_ = c_samples;
    y2_samples_ = y_samples_2;
    c2_samples_ = c_samples_2;
    video_params_ = video_params;
    marker_sample_ = marker_sample;
    
    updateScrollBar();
    update();
}

void FieldTimingWidget::scrollToMarker()
{
    if (!marker_sample_.has_value()) {
        return;
    }
    
    int marker_pos = marker_sample_.value();
    
    // Calculate the visible width in samples
    int visible_width = width() - 2 * MARGIN - 50;  // Account for left margin for labels
    double base_pixels_per_sample = getBasePixelsPerSample();
    double effective_pixels_per_sample = base_pixels_per_sample * zoom_factor_;
    int samples_per_view = static_cast<int>(visible_width / effective_pixels_per_sample);
    
    // Center the marker in the view
    int target_offset = marker_pos - (samples_per_view / 2);
    target_offset = std::max(0, target_offset);
    
    // Set scroll position
    if (scroll_bar_->isEnabled()) {
        scroll_bar_->setValue(target_offset);
    }
}

void FieldTimingWidget::scrollToLine(int line_number)
{
    if (!video_params_.has_value() || video_params_->field_width <= 0) {
        return;
    }
    
    // Convert line number (1-based) to sample position
    // Line 1 starts at sample 0
    int line_start_sample = (line_number - 1) * video_params_->field_width;
    
    // Calculate the visible width in samples
    int visible_width = width() - 2 * MARGIN - 50;  // Account for left margin for labels
    double base_pixels_per_sample = getBasePixelsPerSample();
    double effective_pixels_per_sample = base_pixels_per_sample * zoom_factor_;
    int samples_per_view = static_cast<int>(visible_width / effective_pixels_per_sample);
    
    // Center the line in the view
    int target_offset = line_start_sample - (samples_per_view / 2);
    target_offset = std::max(0, target_offset);
    
    // Set scroll position
    if (scroll_bar_->isEnabled()) {
        scroll_bar_->setValue(target_offset);
    }
}

int FieldTimingWidget::getCenterSample() const
{
    // Check if we have data
    size_t total_samples = std::max({
        field1_samples_.size(),
        field2_samples_.size(),
        y1_samples_.size(),
        y2_samples_.size()
    });
    
    if (total_samples == 0) {
        return -1;
    }
    
    // Calculate the visible width in samples
    int visible_width = width() - 2 * MARGIN - 50;
    double base_pixels_per_sample = getBasePixelsPerSample();
    double effective_pixels_per_sample = base_pixels_per_sample * zoom_factor_;
    int samples_per_view = static_cast<int>(visible_width / effective_pixels_per_sample);
    
    // Get center of current view
    int center_sample = scroll_offset_ + (samples_per_view / 2);
    
    // Clamp to valid range
    if (center_sample < 0) center_sample = 0;
    if (center_sample >= static_cast<int>(total_samples)) {
        center_sample = static_cast<int>(total_samples) - 1;
    }
    
    return center_sample;
}

void FieldTimingWidget::setZoomFactor(double zoom_factor)
{
    zoom_factor_ = std::max(0.01, zoom_factor);  // Clamp to reasonable minimum
    updateScrollBar();
    update();
}

double FieldTimingWidget::getBasePixelsPerSample() const
{
    // Calculate pixels per sample needed to fit ALL samples horizontally at zoom_factor = 1.0
    // This means at zoom 1.0, we show all available lines
    if (!video_params_.has_value() || video_params_->field_width <= 0) {
        return PIXELS_PER_SAMPLE;  // Fallback to default
    }
    
    int visible_width = width() - 2 * MARGIN - 50;  // Account for margins and labels
    if (visible_width <= 0) {
        return PIXELS_PER_SAMPLE;
    }
    
    // Determine total samples (all lines in the field(s))
    size_t total_samples = std::max({
        field1_samples_.size(),
        field2_samples_.size(),
        y1_samples_.size(),
        y2_samples_.size()
    });
    
    if (total_samples == 0) {
        return PIXELS_PER_SAMPLE;
    }
    
    // At zoom_factor = 1.0, all samples should fit in the visible width
    double base_pixels_per_sample = static_cast<double>(visible_width) / static_cast<double>(total_samples);
    
    return base_pixels_per_sample;
}

void FieldTimingWidget::updateScrollBar()
{
    // Determine total sample count
    size_t total_samples = std::max({
        field1_samples_.size(),
        field2_samples_.size(),
        y1_samples_.size(),
        y2_samples_.size()
    });
    
    if (total_samples == 0) {
        scroll_bar_->setRange(0, 0);
        scroll_bar_->setEnabled(false);
        return;
    }
    
    // Calculate visible width (excluding margins)
    int visible_width = width() - 2 * MARGIN - 50;  // Account for left margin for labels
    
    // Get base pixels per sample (to fit all samples at 100% zoom)
    double base_pixels_per_sample = getBasePixelsPerSample();
    double effective_pixels_per_sample = base_pixels_per_sample * zoom_factor_;
    int samples_per_view = static_cast<int>(visible_width / effective_pixels_per_sample);
    
    if (total_samples <= static_cast<size_t>(samples_per_view)) {
        // All samples fit in view
        scroll_bar_->setRange(0, 0);
        scroll_bar_->setEnabled(false);
    } else {
        // Need scrolling
        int max_offset = static_cast<int>(total_samples) - samples_per_view;
        scroll_bar_->setRange(0, max_offset);
        scroll_bar_->setPageStep(samples_per_view);
        scroll_bar_->setSingleStep(std::max(1, samples_per_view / 10));
        scroll_bar_->setEnabled(true);
    }
}

void FieldTimingWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    
    // Position scroll bar at bottom
    int sb_height = scroll_bar_->sizeHint().height();
    scroll_bar_->setGeometry(MARGIN, height() - sb_height - 5, 
                            width() - 2 * MARGIN, sb_height);
    
    updateScrollBar();
}

void FieldTimingWidget::wheelEvent(QWheelEvent *event)
{
    // Horizontal scrolling with mouse wheel
    if (scroll_bar_->isEnabled()) {
        int delta = -event->angleDelta().y() / 8;  // Convert to scroll steps
        scroll_bar_->setValue(scroll_bar_->value() + delta);
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
}

void FieldTimingWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Fill background
    painter.fillRect(rect(), Qt::black);
    
    // Define graph area (leave room for margins and scroll bar)
    // Add extra left margin for Y-axis labels
    int sb_height = scroll_bar_->sizeHint().height();
    int left_margin = 50;  // Extra space for Y-axis labels
    QRect graph_area(MARGIN + left_margin, MARGIN, 
                    width() - 2 * MARGIN - left_margin, 
                    height() - 2 * MARGIN - sb_height - 10);
    
    if (field1_samples_.empty() && field2_samples_.empty() && 
        y1_samples_.empty() && y2_samples_.empty()) {
        // No data to display
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No field data available");
        return;
    }
    
    drawGraph(painter, graph_area);
}

void FieldTimingWidget::drawGraph(QPainter& painter, const QRect& graph_area)
{
    // Draw graph border
    painter.setPen(Qt::darkGray);
    painter.drawRect(graph_area);
    
    // Get mV range
    double min_mv, max_mv;
    getMVRange(min_mv, max_mv);
    
    // Calculate 50 mV tick marks with 0 mV as reference
    // Find the starting tick (multiple of 50 mV that's <= min_mv)
    double grid_step = 50.0;
    double label_step = 100.0;
    double first_tick = std::floor(min_mv / grid_step) * grid_step;
    
    // Draw grid lines and labels
    painter.setPen(Qt::lightGray);
    QFont label_font = painter.font();
    label_font.setPointSize(8);
    painter.setFont(label_font);
    
    painter.setPen(QColor(40, 40, 40));  // Dark gray for grid
    
    // Draw grid lines at 50 mV intervals, labels at 100 mV intervals
    for (double mv_value = first_tick; mv_value <= max_mv; mv_value += grid_step) {
        // Map mV to Y coordinate
        double normalized = (mv_value - min_mv) / (max_mv - min_mv);
        int y = graph_area.bottom() - static_cast<int>(normalized * graph_area.height());
        
        // Only draw if within bounds
        if (y >= graph_area.top() && y <= graph_area.bottom()) {
            // Draw grid line
            painter.setPen(QColor(40, 40, 40));
            painter.drawLine(graph_area.left(), y, graph_area.right(), y);
        }
    }
    
    // Draw labels at 100 mV intervals
    double first_label = std::floor(min_mv / label_step) * label_step;
    painter.setPen(Qt::lightGray);
    for (double mv_value = first_label; mv_value <= max_mv; mv_value += label_step) {
        // Map mV to Y coordinate
        double normalized = (mv_value - min_mv) / (max_mv - min_mv);
        int y = graph_area.bottom() - static_cast<int>(normalized * graph_area.height());
        
        // Only draw if within bounds
        if (y >= graph_area.top() && y <= graph_area.bottom()) {
            QString label = QString::number(static_cast<int>(mv_value)) + " mV";
            // Draw label to the left of the graph area, well separated from the axis
            QRect label_rect(MARGIN, y - 6, graph_area.left() - MARGIN - 5, 12);
            painter.drawText(label_rect, Qt::AlignRight | Qt::AlignVCenter, label);
        }
    }
    
    // Draw level indicator lines if we have video parameters
    if (video_params_.has_value()) {
        const auto& vp = video_params_.value();
        
        // Determine mV conversion factor
        double ire_to_mv = 7.0;
        if (vp.system == orc::presenters::VideoSystem::NTSC || 
            vp.system == orc::presenters::VideoSystem::PAL_M) {
            ire_to_mv = 7.143;
        }
        
        // Helper to draw horizontal level line
        auto drawLevelLine = [&](double mv, const QColor& color, Qt::PenStyle style = Qt::DashLine) {
            // Map mV to Y coordinate
            double normalized = (mv - min_mv) / (max_mv - min_mv);
            int y = graph_area.bottom() - static_cast<int>(normalized * graph_area.height());
            
            if (y >= graph_area.top() && y <= graph_area.bottom()) {
                painter.setPen(QPen(color, 1, style));
                painter.drawLine(graph_area.left(), y, graph_area.right(), y);
            }
        };
        
        // Draw level lines
        if (vp.blanking_ire >= 0 && vp.white_ire >= 0) {
            // Blanking level - convert the blanking_ire sample value to mV
            double blanking_mv = convertSampleToMV(vp.blanking_ire);
            drawLevelLine(blanking_mv, Qt::darkGray, Qt::DashLine);
            
            // Black level (if different from blanking)
            if (vp.black_ire >= 0 && vp.black_ire != vp.blanking_ire) {
                double black_mv = convertSampleToMV(vp.black_ire);
                drawLevelLine(black_mv, Qt::gray, Qt::DashDotLine);
            }
            
            // White level
            double white_mv = convertSampleToMV(vp.white_ire);
            drawLevelLine(white_mv, Qt::lightGray, Qt::DashLine);
        }
    }
    
    // Draw vertical field line markers
    if (video_params_.has_value()) {
        const auto& vp = video_params_.value();
        if (vp.field_width > 0 && vp.field_height > 0) {
            // Determine which samples to use for line count
            size_t total_samples = std::max({
                field1_samples_.size(),
                field2_samples_.size(),
                y1_samples_.size(),
                y2_samples_.size()
            });
            
            int visible_width = graph_area.width();
            double base_pixels_per_sample = getBasePixelsPerSample();
            double effective_pixels_per_sample = base_pixels_per_sample * zoom_factor_;
            int samples_per_view = static_cast<int>(visible_width / effective_pixels_per_sample);
            int start_sample = scroll_offset_;
            int end_sample = std::min(start_sample + samples_per_view, static_cast<int>(total_samples));
            
            // Calculate how many lines are visible
            int lines_visible = samples_per_view / vp.field_width;
            if (lines_visible == 0) lines_visible = 1;
            
            // Determine marker spacing based on zoom level
            int marker_interval = 1;  // Default: show every line
            if (lines_visible > 100) {
                // Show every 50th line
                marker_interval = 50;
            }
            // For 100 or fewer lines, show every line (marker_interval = 1)
            
            QFont line_num_font = painter.font();
            line_num_font.setPointSize(8);
            painter.setFont(line_num_font);
            
            // Draw vertical markers at field line boundaries
            for (int line = 0; line * vp.field_width < end_sample; ++line) {
                int sample_pos = line * vp.field_width;
                if (sample_pos >= start_sample && sample_pos <= end_sample) {
                    // Only draw markers at the specified interval
                    if (line % marker_interval == 0 || marker_interval == 1) {
                        int x = graph_area.left() + static_cast<int>((sample_pos - start_sample) * effective_pixels_per_sample);
                        
                        // Draw vertical line in yellow
                        painter.setPen(QPen(QColor(200, 200, 0), 1, Qt::DotLine));
                        painter.drawLine(x, graph_area.top(), x, graph_area.bottom());
                        
                        // Draw line number on x-axis below the graph
                        painter.setPen(QColor(200, 200, 0));
                        QString line_label = QString::number(line + 1);
                        QRect text_rect(x - 15, graph_area.bottom() + 5, 30, 12);
                        painter.drawText(text_rect, Qt::AlignCenter, line_label);
                    }
                }
            }
        }
    }

    // Draw selected position marker (green) if provided
    if (marker_sample_.has_value()) {
        // Determine visible sample range
        int visible_width = graph_area.width();
        double base_pixels_per_sample = getBasePixelsPerSample();
        double effective_pixels_per_sample = base_pixels_per_sample * zoom_factor_;
        int samples_per_view = static_cast<int>(visible_width / effective_pixels_per_sample);
        int start_sample = scroll_offset_;
        int end_sample = start_sample + samples_per_view;
        
        int sample_pos = marker_sample_.value();
        if (sample_pos >= start_sample && sample_pos <= end_sample) {
            int x = graph_area.left() + static_cast<int>((sample_pos - start_sample) * effective_pixels_per_sample);
            painter.setPen(QPen(QColor(0, 255, 0), 2, Qt::SolidLine));
            painter.drawLine(x, graph_area.top(), x, graph_area.bottom());
        }
    }
    
    // Draw X-axis label
    painter.setPen(Qt::lightGray);
    painter.drawText(graph_area.center().x() - 50, height() - 5, "Sample Position");
    
    // Determine which samples to draw and their colors
    // Priority: if Y/C samples exist, use those; otherwise use composite
    bool has_yc = !y1_samples_.empty() || !y2_samples_.empty();
    bool has_two_fields = !field2_samples_.empty() || !y2_samples_.empty();
    
    if (has_yc) {
        // Draw Y and C channels separately
        if (!y1_samples_.empty()) {
            drawSamples(painter, graph_area, y1_samples_, QColor(0, 255, 0), 0);  // Green for Y
        }
        if (!c1_samples_.empty()) {
            drawSamples(painter, graph_area, c1_samples_, QColor(255, 128, 0), 0);  // Orange for C
        }
        if (has_two_fields) {
            if (!y2_samples_.empty()) {
                drawSamples(painter, graph_area, y2_samples_, QColor(128, 255, 128), 0);  // Light green for Y2
            }
            if (!c2_samples_.empty()) {
                drawSamples(painter, graph_area, c2_samples_, QColor(255, 200, 128), 0);  // Light orange for C2
            }
        }
    } else {
        // Draw composite samples
        if (!field1_samples_.empty()) {
            drawSamples(painter, graph_area, field1_samples_, QColor(0, 200, 255), 0);  // Cyan for field 1
        }
        if (!field2_samples_.empty()) {
            drawSamples(painter, graph_area, field2_samples_, QColor(255, 200, 0), 0);  // Yellow for field 2
        }
    }
}

void FieldTimingWidget::drawSamples(QPainter& painter, const QRect& graph_area,
                                   const std::vector<uint16_t>& samples,
                                   const QColor& color, int y_offset)
{
    if (samples.empty()) return;
    
    painter.setPen(QPen(color, 1));
    
    // Calculate visible sample range
    int visible_width = graph_area.width();
    double base_pixels_per_sample = getBasePixelsPerSample();
    double effective_pixels_per_sample = base_pixels_per_sample * zoom_factor_;
    int samples_per_view = static_cast<int>(visible_width / effective_pixels_per_sample);
    int start_sample = scroll_offset_;
    int end_sample = std::min(start_sample + samples_per_view, static_cast<int>(samples.size()));
    
    if (start_sample >= static_cast<int>(samples.size())) return;
    
    // Get mV range for normalization
    double min_mv, max_mv;
    getMVRange(min_mv, max_mv);
    
    // Calculate how many field lines are visible
    int lines_visible = 0;
    if (video_params_.has_value() && video_params_->field_width > 0) {
        lines_visible = samples_per_view / video_params_->field_width;
    }
    
    // Use min/max per pixel optimization only when:
    // 1. Zoomed out (multiple samples per pixel)
    // 2. Displaying 100 or more field lines
    if (effective_pixels_per_sample < 1.0 && lines_visible >= 100) {
        // Multiple samples map to each pixel - draw vertical line from min to max
        double samples_per_pixel = 1.0 / effective_pixels_per_sample;
        
        for (int px = 0; px < visible_width; ++px) {
            int x = graph_area.left() + px;
            
            // Calculate sample range for this pixel column
            int bucket_start = start_sample + static_cast<int>(px * samples_per_pixel);
            int bucket_end = std::min(start_sample + static_cast<int>((px + 1) * samples_per_pixel), 
                                     static_cast<int>(samples.size()));
            
            if (bucket_start >= bucket_end || bucket_start >= static_cast<int>(samples.size())) continue;
            
            // Find min and max sample values in this pixel column
            uint16_t min_sample = samples[bucket_start];
            uint16_t max_sample = samples[bucket_start];
            
            for (int i = bucket_start; i < bucket_end; ++i) {
                min_sample = std::min(min_sample, samples[i]);
                max_sample = std::max(max_sample, samples[i]);
            }
            
            // Convert to Y coordinates
            double min_mv_value = convertSampleToMV(min_sample);
            double max_mv_value = convertSampleToMV(max_sample);
            
            double min_normalized = (min_mv_value - min_mv) / (max_mv - min_mv);
            double max_normalized = (max_mv_value - min_mv) / (max_mv - min_mv);
            
            int y_top = graph_area.bottom() - static_cast<int>(max_normalized * graph_area.height()) + y_offset;
            int y_bottom = graph_area.bottom() - static_cast<int>(min_normalized * graph_area.height()) + y_offset;
            
            // Draw vertical line from min to max (preserves peaks and troughs)
            painter.drawLine(x, y_top, x, y_bottom);
        }
    } else {
        // Draw all individual samples as connected lines
        QPainterPath path;
        bool first_point = true;
        
        for (int i = start_sample; i < end_sample; ++i) {
            int x = graph_area.left() + static_cast<int>((i - start_sample) * effective_pixels_per_sample);
            
            // Convert sample to mV and map to Y coordinate
            double mv_value = convertSampleToMV(samples[i]);
            double normalized = (mv_value - min_mv) / (max_mv - min_mv);
            int y = graph_area.bottom() - static_cast<int>(normalized * graph_area.height()) + y_offset;
            
            if (first_point) {
                path.moveTo(x, y);
                first_point = false;
            } else {
                path.lineTo(x, y);
            }
        }
        
        painter.drawPath(path);
    }
}
