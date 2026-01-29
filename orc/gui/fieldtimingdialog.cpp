/*
 * File:        fieldtimingdialog.cpp
 * Module:      orc-gui
 * Purpose:     Field timing visualization dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "fieldtimingdialog.h"
#include "field_frame_presentation.h"
#include "fieldtimingwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSlider>
#include <QSettings>
#include <QResizeEvent>

FieldTimingDialog::FieldTimingDialog(QWidget *parent)
    : QDialog(parent)
    , current_field_index_(0)
    , current_first_field_height_(0)
    , current_second_field_height_(0)
{
    setupUI();
    setWindowTitle("Field Timing View");
    
    // Use Qt::Window flag to allow independent positioning
    setWindowFlags(Qt::Window);
    
    // Make dialog non-modal so it doesn't block the preview dialog
    setModal(false);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size
    resize(900, 500);
    
    // Restore geometry if saved
    QSettings settings;
    restoreGeometry(settings.value("FieldTimingDialog/geometry").toByteArray());
}

FieldTimingDialog::~FieldTimingDialog()
{
    // Save geometry
    QSettings settings;
    settings.setValue("FieldTimingDialog/geometry", saveGeometry());
}

void FieldTimingDialog::setupUI()
{
    auto* main_layout = new QVBoxLayout(this);
    
    // Timing widget
    timing_widget_ = new FieldTimingWidget(this);
    main_layout->addWidget(timing_widget_, 1);
    
    // Control row with buttons and zoom slider
    auto* control_layout = new QHBoxLayout();
    
    jump_button_ = new QPushButton("Jump to Crosshairs");
    jump_button_->setEnabled(false);  // Initially disabled
    jump_button_->setAutoDefault(false);  // Don't capture Enter key
    connect(jump_button_, &QPushButton::clicked, [this]() {
        timing_widget_->scrollToMarker();
    });
    control_layout->addWidget(jump_button_);
    
    set_crosshairs_button_ = new QPushButton("Set Crosshairs");
    set_crosshairs_button_->setAutoDefault(false);  // Don't capture Enter key
    connect(set_crosshairs_button_, &QPushButton::clicked, [this]() {
        emit setCrosshairsRequested();
    });
    control_layout->addWidget(set_crosshairs_button_);
    
    control_layout->addSpacing(20);
    
    // Line jump controls
    auto* line_label = new QLabel("Line:");
    control_layout->addWidget(line_label);
    
    line_spinbox_ = new QSpinBox();
    line_spinbox_->setMinimum(1);
    line_spinbox_->setMaximum(625);  // Default to PAL max, will be updated with video params
    line_spinbox_->setValue(1);
    line_spinbox_->setMinimumWidth(80);
    // Jump to line when Enter is pressed
    connect(line_spinbox_, &QSpinBox::editingFinished, [this]() {
        timing_widget_->scrollToLine(line_spinbox_->value());
    });
    control_layout->addWidget(line_spinbox_);
    
    jump_line_button_ = new QPushButton("Jump to Line");
    jump_line_button_->setAutoDefault(false);  // Don't capture Enter key
    connect(jump_line_button_, &QPushButton::clicked, [this]() {
        timing_widget_->scrollToLine(line_spinbox_->value());
    });
    control_layout->addWidget(jump_line_button_);
    
    control_layout->addStretch();
    
    // Zoom control
    auto* zoom_label = new QLabel("Lines:");
    control_layout->addWidget(zoom_label);
    
    // Zoom in button (decrease lines shown)
    auto* zoom_in_button = new QPushButton("-");
    zoom_in_button->setMaximumWidth(30);
    zoom_in_button->setAutoRepeat(true);
    zoom_in_button->setAutoRepeatDelay(250);
    zoom_in_button->setAutoRepeatInterval(50);
    connect(zoom_in_button, &QPushButton::clicked, [this]() {
        int current = zoom_slider_->value();
        int step = (current >= 100) ? 10 : 1;  // Larger steps when zoomed out
        zoom_slider_->setValue(std::max(current - step, zoom_slider_->minimum()));
    });
    control_layout->addWidget(zoom_in_button);
    
    zoom_slider_ = new QSlider(Qt::Horizontal);
    zoom_slider_->setMinimum(2);     // Minimum 2 lines visible
    zoom_slider_->setMaximum(625);   // Default to PAL max, will be updated with video params
    zoom_slider_->setValue(625);     // Default to showing all lines
    zoom_slider_->setTickPosition(QSlider::TicksBelow);
    zoom_slider_->setTickInterval(50);
    zoom_slider_->setMaximumWidth(150);
    connect(zoom_slider_, &QSlider::valueChanged, [this](int lines_to_show) {
        // Calculate zoom factor based on total lines available from VFR descriptors
        // At zoom_factor = 1.0, we show ALL lines
        // To show fewer lines, we zoom in (zoom_factor > 1.0)
        if (current_first_field_height_ > 0) {
            int total_lines = current_first_field_height_;
            // For frame mode, use sum of both field heights
            if (current_field_index_2_.has_value() && current_second_field_height_ > 0) {
                total_lines += current_second_field_height_;
            }
            double zoom_factor = static_cast<double>(total_lines) / static_cast<double>(lines_to_show);
            timing_widget_->setZoomFactor(zoom_factor);
        }
    });
    control_layout->addWidget(zoom_slider_);
    
    // Zoom out button (increase lines shown)
    auto* zoom_out_button = new QPushButton("+");
    zoom_out_button->setMaximumWidth(30);
    zoom_out_button->setAutoRepeat(true);
    zoom_out_button->setAutoRepeatDelay(250);
    zoom_out_button->setAutoRepeatInterval(50);
    connect(zoom_out_button, &QPushButton::clicked, [this]() {
        int current = zoom_slider_->value();
        int step = (current >= 100) ? 10 : 1;  // Larger steps when zoomed out
        zoom_slider_->setValue(std::min(current + step, zoom_slider_->maximum()));
    });
    control_layout->addWidget(zoom_out_button);
    
    zoom_value_label_ = new QLabel("625");
    zoom_value_label_->setMinimumWidth(40);
    connect(zoom_slider_, &QSlider::valueChanged, [this](int value) {
        zoom_value_label_->setText(QString::number(value));
    });
    control_layout->addWidget(zoom_value_label_);
    
    control_layout->addSpacing(10);
    
    auto* close_button = new QPushButton("Close");
    connect(close_button, &QPushButton::clicked, this, &QDialog::close);
    control_layout->addWidget(close_button);
    
    main_layout->addLayout(control_layout);
}

void FieldTimingDialog::setFieldData(const QString& node_id,
                                    uint64_t field_index,
                                    const std::vector<uint16_t>& samples,
                                    std::optional<uint64_t> field_index_2,
                                    const std::vector<uint16_t>& samples_2,
                                    const std::vector<uint16_t>& y_samples,
                                    const std::vector<uint16_t>& c_samples,
                                    const std::vector<uint16_t>& y_samples_2,
                                    const std::vector<uint16_t>& c_samples_2,
                                    const std::optional<orc::presenters::VideoParametersView>& video_params,
                                    const std::optional<int>& marker_sample,
                                    int first_field_height,
                                    int second_field_height)
{
    current_node_id_ = node_id;
    current_field_index_ = field_index;
    current_field_index_2_ = field_index_2;
    current_first_field_height_ = first_field_height;
    current_second_field_height_ = second_field_height;
    
    // Update window title with field info (1-indexed for display)
    QString title = QString("Field Timing View - Stage: %1, Field: %2")
                        .arg(node_id)
                        .arg(field_index + 1);  // Convert to 1-based
    if (field_index_2.has_value()) {
        title += QString(" + %1").arg(field_index_2.value() + 1);  // Convert to 1-based
    }
    setWindowTitle(title);
    
    // Update widget data
    timing_widget_->setFieldData(samples, samples_2, y_samples, c_samples, 
                                y_samples_2, c_samples_2, video_params, marker_sample);
    
    // Enable/disable jump button based on whether marker is present
    jump_button_->setEnabled(marker_sample.has_value());
    
    // Update line spinbox range based on field heights from VFR descriptor
    int total_lines = first_field_height;
    
    if (field_index_2.has_value()) {
        // Frame mode: total height is sum of both field heights
        total_lines = first_field_height + second_field_height;
    }
    
    if (total_lines > 0) {
        // Set spinbox maximum to total lines available
        line_spinbox_->setMaximum(total_lines);
        
        // Update zoom slider range and preserve current zoom level
        int current_zoom = zoom_slider_->value();
        zoom_slider_->setMaximum(total_lines);
        // Restore zoom level if possible, otherwise show all lines
        if (current_zoom <= total_lines) {
            zoom_slider_->setValue(current_zoom);
        } else {
            zoom_slider_->setValue(total_lines);  // Show all lines if previous zoom was larger
        }
        zoom_value_label_->setText(QString::number(zoom_slider_->value()));
        
        // Trigger zoom update with current slider value
        int lines_to_show = zoom_slider_->value();
        double zoom_factor = static_cast<double>(total_lines) / static_cast<double>(lines_to_show);
        timing_widget_->setZoomFactor(zoom_factor);
        
        // Update tick interval based on total lines
        int tick_interval = (total_lines > 600) ? 100 : 50;
        zoom_slider_->setTickInterval(tick_interval);
    }
}
