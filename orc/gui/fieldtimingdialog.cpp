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
#include <QComboBox>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
// Minimum number of lines the timing view can display when fully zoomed in.
constexpr int kMinLinesVisible = 2;

// Internal slider coordinate range (position-space), mapped logarithmically to line counts.
constexpr int kZoomSliderMin = 0;
constexpr int kZoomSliderMax = 1000;
}

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

    zoom_settle_timer_ = new QTimer(this);
    zoom_settle_timer_->setSingleShot(true);
    connect(zoom_settle_timer_, &QTimer::timeout, this, [this]() {
        finalizeRenderQuality();
    });
    
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

    control_layout->addSpacing(20);

    signal_label_ = new QLabel("Channel:");
    signal_label_->setVisible(false);
    control_layout->addWidget(signal_label_);

    signal_combo_ = new QComboBox();
    signal_combo_->addItem("Luma (Y)");
    signal_combo_->addItem("Chroma (C)");
    signal_combo_->addItem("Both (Y & C)");
    signal_combo_->addItem("Y+C");
    signal_combo_->setVisible(false);
    connect(signal_combo_, &QComboBox::currentIndexChanged, [this](int index) {
        current_signal_index_ = index;
        timing_widget_->setChannelMode(static_cast<FieldTimingWidget::ChannelMode>(index));
    });
    control_layout->addWidget(signal_combo_);
    
    control_layout->addStretch();
    
    // Zoom control
    auto* zoom_label = new QLabel("Lines:");
    control_layout->addWidget(zoom_label);

    auto moveZoomByButton = [this](int direction) {
        // direction: -1 = zoom in (fewer lines), +1 = zoom out (more lines)
        int total_lines = currentTotalLines();
        int delta_lines = std::max(1, current_lines_to_show_ / 25);
        int target_lines = std::clamp(current_lines_to_show_ + (direction * delta_lines),
                                      kMinLinesVisible,
                                      total_lines);

        if (target_lines == current_lines_to_show_) {
            return;
        }

        int current_pos = zoom_slider_->value();
        int target_pos = linesToSliderPosition(target_lines);

        // If mapping rounds to the same slider position, walk to the next position that
        // guarantees at least one visible line change.
        if (target_pos == current_pos) {
            int scan_pos = current_pos;
            while (true) {
                scan_pos += direction;
                if (scan_pos < zoom_slider_->minimum() || scan_pos > zoom_slider_->maximum()) {
                    break;
                }

                int mapped_lines = sliderPositionToLines(scan_pos);
                if ((direction < 0 && mapped_lines < current_lines_to_show_) ||
                    (direction > 0 && mapped_lines > current_lines_to_show_)) {
                    target_pos = scan_pos;
                    break;
                }
            }
        }

        zoom_slider_->setValue(target_pos);
    };
    
    // Zoom in button (decrease lines shown)
    auto* zoom_in_button = new QPushButton("-");
    zoom_in_button->setMaximumWidth(30);
    zoom_in_button->setAutoRepeat(true);
    zoom_in_button->setAutoRepeatDelay(250);
    zoom_in_button->setAutoRepeatInterval(50);
    connect(zoom_in_button, &QPushButton::clicked, [moveZoomByButton]() {
        moveZoomByButton(-1);
    });
    connect(zoom_in_button, &QPushButton::pressed, [this]() {
        beginDraftRendering();
    });
    connect(zoom_in_button, &QPushButton::released, [this]() {
        finalizeRenderQuality();
    });
    control_layout->addWidget(zoom_in_button);
    
    zoom_slider_ = new QSlider(Qt::Horizontal);
    zoom_slider_->setMinimum(kZoomSliderMin);
    zoom_slider_->setMaximum(kZoomSliderMax);
    zoom_slider_->setValue(linesToSliderPosition(current_lines_to_show_));
    zoom_slider_->setTickPosition(QSlider::TicksBelow);
    zoom_slider_->setTickInterval(100);
    zoom_slider_->setMaximumWidth(150);
    connect(zoom_slider_, &QSlider::valueChanged, [this](int slider_position) {
        int lines_to_show = sliderPositionToLines(slider_position);
        current_lines_to_show_ = lines_to_show;
        zoom_value_label_->setText(QString::number(lines_to_show));
        beginDraftRendering();
        applyZoomFromLines(lines_to_show);
        scheduleFinalRender();
    });
    connect(zoom_slider_, &QSlider::sliderPressed, [this]() {
        beginDraftRendering();
    });
    connect(zoom_slider_, &QSlider::sliderReleased, [this]() {
        finalizeRenderQuality();
    });
    control_layout->addWidget(zoom_slider_);
    
    // Zoom out button (increase lines shown)
    auto* zoom_out_button = new QPushButton("+");
    zoom_out_button->setMaximumWidth(30);
    zoom_out_button->setAutoRepeat(true);
    zoom_out_button->setAutoRepeatDelay(250);
    zoom_out_button->setAutoRepeatInterval(50);
    connect(zoom_out_button, &QPushButton::clicked, [moveZoomByButton]() {
        moveZoomByButton(1);
    });
    connect(zoom_out_button, &QPushButton::pressed, [this]() {
        beginDraftRendering();
    });
    connect(zoom_out_button, &QPushButton::released, [this]() {
        finalizeRenderQuality();
    });
    control_layout->addWidget(zoom_out_button);
    
    zoom_value_label_ = new QLabel("625");
    zoom_value_label_->setMinimumWidth(40);
    zoom_value_label_->setText(QString::number(current_lines_to_show_));
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

    const bool is_yc_source = !y_samples.empty() || !c_samples.empty() || !y_samples_2.empty() || !c_samples_2.empty();
    signal_label_->setVisible(is_yc_source);
    signal_combo_->setVisible(is_yc_source);
    if (is_yc_source) {
        current_signal_index_ = std::clamp(current_signal_index_, 0, signal_combo_->count() - 1);
        signal_combo_->setCurrentIndex(current_signal_index_);
        timing_widget_->setChannelMode(static_cast<FieldTimingWidget::ChannelMode>(current_signal_index_));
    } else {
        timing_widget_->setChannelMode(FieldTimingWidget::ChannelMode::YPlusC);
    }

    // Data refreshes should not keep draft mode active.
    finalizeRenderQuality();
    
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

        // Preserve visible line target across source changes.
        current_lines_to_show_ = std::clamp(current_lines_to_show_, kMinLinesVisible, total_lines);

        zoom_slider_->blockSignals(true);
        zoom_slider_->setValue(linesToSliderPosition(current_lines_to_show_));
        zoom_slider_->blockSignals(false);
        zoom_value_label_->setText(QString::number(current_lines_to_show_));
        
        // Trigger zoom update with current slider value
        applyZoomFromLines(current_lines_to_show_);
    }
}

int FieldTimingDialog::currentTotalLines() const
{
    int total_lines = current_first_field_height_;
    if (current_field_index_2_.has_value() && current_second_field_height_ > 0) {
        total_lines += current_second_field_height_;
    }

    if (total_lines <= 0) {
        total_lines = 625;
    }

    return std::max(kMinLinesVisible, total_lines);
}

int FieldTimingDialog::sliderPositionToLines(int slider_position) const
{
    int total_lines = currentTotalLines();

    if (total_lines <= kMinLinesVisible) {
        return total_lines;
    }

    double t = static_cast<double>(slider_position - kZoomSliderMin) /
               static_cast<double>(kZoomSliderMax - kZoomSliderMin);
    t = std::clamp(t, 0.0, 1.0);

    double ratio = static_cast<double>(total_lines) / static_cast<double>(kMinLinesVisible);
    double mapped = static_cast<double>(kMinLinesVisible) * std::pow(ratio, t);
    int lines = static_cast<int>(std::round(mapped));
    return std::clamp(lines, kMinLinesVisible, total_lines);
}

int FieldTimingDialog::linesToSliderPosition(int lines_to_show) const
{
    int total_lines = currentTotalLines();
    int lines = std::clamp(lines_to_show, kMinLinesVisible, total_lines);

    if (total_lines <= kMinLinesVisible) {
        return kZoomSliderMin;
    }

    double ratio = static_cast<double>(total_lines) / static_cast<double>(kMinLinesVisible);
    double normalized = std::log(static_cast<double>(lines) / static_cast<double>(kMinLinesVisible)) /
                        std::log(ratio);
    normalized = std::clamp(normalized, 0.0, 1.0);

    int slider_position = static_cast<int>(std::round(
        normalized * static_cast<double>(kZoomSliderMax - kZoomSliderMin) + kZoomSliderMin));
    return std::clamp(slider_position, kZoomSliderMin, kZoomSliderMax);
}

void FieldTimingDialog::applyZoomFromLines(int lines_to_show)
{
    // At zoom_factor = 1.0, we show all lines; fewer lines means zoom in.
    if (current_first_field_height_ <= 0 || lines_to_show <= 0) {
        return;
    }

    int total_lines = current_first_field_height_;
    if (current_field_index_2_.has_value() && current_second_field_height_ > 0) {
        total_lines += current_second_field_height_;
    }

    if (total_lines <= 0) {
        return;
    }

    double zoom_factor = static_cast<double>(total_lines) / static_cast<double>(lines_to_show);
    timing_widget_->setZoomFactor(zoom_factor);
}

void FieldTimingDialog::beginDraftRendering()
{
    timing_widget_->setDraftRenderMode(true);
}

void FieldTimingDialog::scheduleFinalRender()
{
    constexpr int kZoomSettleDelayMs = 120;
    zoom_settle_timer_->start(kZoomSettleDelayMs);
}

void FieldTimingDialog::finalizeRenderQuality()
{
    zoom_settle_timer_->stop();
    timing_widget_->setDraftRenderMode(false);
}
