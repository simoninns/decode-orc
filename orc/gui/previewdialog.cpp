/*
 * File:        previewdialog.cpp
 * Module:      orc-gui
 * Purpose:     Separate preview window for field/frame viewing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "previewdialog.h"
#include "fieldpreviewwidget.h"
#include "linescopedialog.h"
#include "fieldtimingdialog.h"
#include "logging.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QStatusBar>
#include <QPushButton>
#include <QCloseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QMenu>
#include <QSettings>
#include <algorithm>

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent)
    , line_scope_dialog_(nullptr)
    , field_timing_dialog_(nullptr)
{
    setupUI();
    setWindowTitle("Field/Frame Preview");
    
    // Use Qt::Window flag to allow independent positioning (like ld-analyse dialogs)
    // Keep the dialog in front of the main window
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size - geometry will be restored by MainWindow
    resize(800, 700);
}

void PreviewDialog::setSignalControlsVisible(bool visible)
{
    signal_label_->setVisible(visible);
    signal_combo_->setVisible(visible);
}

PreviewDialog::~PreviewDialog() = default;

void PreviewDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Menu bar
    menu_bar_ = new QMenuBar(this);
    auto* fileMenu = menu_bar_->addMenu("&File");
    export_png_action_ = fileMenu->addAction("&Export PNG...");
    export_png_action_->setShortcut(QKeySequence("Ctrl+E"));
    connect(export_png_action_, &QAction::triggered, this, &PreviewDialog::exportPNGRequested);
    
    auto* observersMenu = menu_bar_->addMenu("&Observers");
    show_vbi_action_ = observersMenu->addAction("&VBI Decoder");
    show_vbi_action_->setShortcut(QKeySequence("Ctrl+V"));
    connect(show_vbi_action_, &QAction::triggered, this, &PreviewDialog::showVBIDialogRequested);
    
    show_quality_metrics_action_ = observersMenu->addAction("&Quality Metrics");
    show_quality_metrics_action_->setShortcut(QKeySequence("Ctrl+Q"));
    connect(show_quality_metrics_action_, &QAction::triggered, this, &PreviewDialog::showQualityMetricsDialogRequested);
    
    show_ntsc_observer_action_ = observersMenu->addAction("&NTSC Observer");
    show_ntsc_observer_action_->setShortcut(QKeySequence("Ctrl+N"));
    connect(show_ntsc_observer_action_, &QAction::triggered, this, &PreviewDialog::showNtscObserverDialogRequested);
    
    auto* hintsMenu = menu_bar_->addMenu("&Hints");
    show_hints_action_ = hintsMenu->addAction("&Video Parameter Hints");
    show_hints_action_->setShortcut(QKeySequence("Ctrl+H"));
    connect(show_hints_action_, &QAction::triggered, this, &PreviewDialog::showHintsDialogRequested);
    
    auto* viewMenu = menu_bar_->addMenu("&View");
    show_field_timing_action_ = viewMenu->addAction("&Field Timing");
    show_field_timing_action_->setShortcut(QKeySequence("Ctrl+T"));
    connect(show_field_timing_action_, &QAction::triggered, this, &PreviewDialog::fieldTimingRequested);
    
    mainLayout->setMenuBar(menu_bar_);
    
    // Preview widget
    preview_widget_ = new FieldPreviewWidget(this);
    preview_widget_->setMinimumSize(640, 480);
    mainLayout->addWidget(preview_widget_, 1);
    
    // Preview info label
    preview_info_label_ = new QLabel("No preview available");
    mainLayout->addWidget(preview_info_label_);
    
    // Slider controls with navigation buttons
    auto* sliderLayout = new QHBoxLayout();
    
    // Navigation buttons
    first_button_ = new QPushButton("<<");
    prev_button_ = new QPushButton("<");
    next_button_ = new QPushButton(">");
    last_button_ = new QPushButton(">>");
    
    // Set auto-repeat on prev/next buttons for navigation
    // Increased delay to reduce sensitivity for single-frame stepping
    prev_button_->setAutoRepeat(true);
    prev_button_->setAutoRepeatDelay(200);
    prev_button_->setAutoRepeatInterval(30);
    
    next_button_->setAutoRepeat(true);
    next_button_->setAutoRepeatDelay(200);
    next_button_->setAutoRepeatInterval(30);
    
    // Set fixed width for navigation buttons
    first_button_->setFixedWidth(40);
    prev_button_->setFixedWidth(40);
    next_button_->setFixedWidth(40);
    last_button_->setFixedWidth(40);
    
    slider_min_label_ = new QLabel("0");
    slider_max_label_ = new QLabel("0");
    preview_slider_ = new QSlider(Qt::Horizontal);
    preview_slider_->setEnabled(false);
    // Set tracking to false for better performance during scrubbing
    // This makes the slider only emit valueChanged when released, not during drag
    // For real-time preview during drag, we can use sliderMoved signal separately
    preview_slider_->setTracking(true);  // Keep true for now, but we'll throttle updates in MainWindow
    
    sliderLayout->addWidget(first_button_);
    sliderLayout->addWidget(prev_button_);
    sliderLayout->addWidget(next_button_);
    sliderLayout->addWidget(last_button_);
    sliderLayout->addWidget(slider_min_label_);
    sliderLayout->addWidget(preview_slider_, 1);
    sliderLayout->addWidget(slider_max_label_);
    mainLayout->addLayout(sliderLayout);
    
    // Control row: Preview mode and aspect ratio
    auto* controlLayout = new QHBoxLayout();
    
    controlLayout->addWidget(new QLabel("Preview Mode:"));
    preview_mode_combo_ = new QComboBox();
    controlLayout->addWidget(preview_mode_combo_);
    
    signal_label_ = new QLabel("Signal:");
    signal_label_->setVisible(false);  // Hidden by default, shown for YC sources
    controlLayout->addWidget(signal_label_);
    signal_combo_ = new QComboBox();
    signal_combo_->addItem("Y+C");
    signal_combo_->addItem("Y");
    signal_combo_->addItem("C");
    signal_combo_->setVisible(false);  // Hidden by default, shown for YC sources
    controlLayout->addWidget(signal_combo_);
    
    controlLayout->addWidget(new QLabel("Aspect Ratio:"));
    aspect_ratio_combo_ = new QComboBox();
    controlLayout->addWidget(aspect_ratio_combo_);
    
    // Add Zoom 1:1 button
    zoom1to1_button_ = new QPushButton("Zoom 1:1");
    zoom1to1_button_->setToolTip("Resize preview to original image size");
    controlLayout->addWidget(zoom1to1_button_);
    
    // Add Dropouts toggle button
    dropouts_button_ = new QPushButton("Dropouts: Off");
    dropouts_button_->setCheckable(true);
    dropouts_button_->setChecked(false);
    dropouts_button_->setToolTip("Show/hide dropout regions");
    controlLayout->addWidget(dropouts_button_);
    
    controlLayout->addStretch();
    mainLayout->addLayout(controlLayout);
    
    // Status bar
    status_bar_ = new QStatusBar(this);
    status_bar_->showMessage("No stage selected");
    mainLayout->addWidget(status_bar_);
    
    // Connect signals
    connect(preview_slider_, &QSlider::valueChanged, this, &PreviewDialog::previewIndexChanged);
    connect(preview_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::previewModeChanged);
    connect(signal_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::signalChanged);
    connect(aspect_ratio_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::aspectRatioModeChanged);
    
    // Connect navigation buttons
    connect(first_button_, &QPushButton::clicked, [this]() {
        const int new_value = preview_slider_->minimum();
        if (preview_slider_->value() == new_value) {
            return;  // Already at first frame, avoid duplicate request
        }

        preview_slider_->setValue(new_value);
        emit sequentialPreviewRequested(new_value);
    });
    connect(prev_button_, &QPushButton::clicked, [this]() {
        const int current_value = preview_slider_->value();
        const int new_value = std::max(preview_slider_->minimum(), current_value - 1);
        if (new_value == current_value) {
            return;  // Already at boundary, do not re-request
        }

        preview_slider_->setValue(new_value);
        emit sequentialPreviewRequested(new_value);
    });
    connect(next_button_, &QPushButton::clicked, [this]() {
        const int current_value = preview_slider_->value();
        const int new_value = std::min(preview_slider_->maximum(), current_value + 1);
        if (new_value == current_value) {
            return;  // Already at boundary, do not re-request
        }

        preview_slider_->setValue(new_value);
        emit sequentialPreviewRequested(new_value);
    });
    connect(last_button_, &QPushButton::clicked, [this]() {
        const int new_value = preview_slider_->maximum();
        if (preview_slider_->value() == new_value) {
            return;  // Already at last frame, avoid duplicate request
        }

        preview_slider_->setValue(new_value);
        emit sequentialPreviewRequested(new_value);
    });
    
    // Connect dropouts button
    connect(dropouts_button_, &QPushButton::toggled, [this](bool checked) {
        dropouts_button_->setText(checked ? "Dropouts: On" : "Dropouts: Off");
        emit showDropoutsChanged(checked);
    });
    
    // Connect Zoom 1:1 button
    connect(zoom1to1_button_, &QPushButton::clicked, [this]() {
        QSize img_size = preview_widget_->originalImageSize();
        if (img_size.isEmpty()) {
            return;  // No image to zoom to
        }
        
        // The image from core already has aspect ratio scaling applied,
        // so we can use the image size directly for 1:1 zoom
        QSize display_size = img_size;
        
        // Calculate total window size based on widget size
        // We need to account for all other UI elements
        int extra_height = height() - preview_widget_->height();
        int extra_width = width() - preview_widget_->width();
        
        // Set the preview widget to the original size
        preview_widget_->setMinimumSize(display_size);
        preview_widget_->setMaximumSize(display_size);
        
        // Adjust the dialog size
        adjustSize();
        
        // Reset size constraints after resize
        preview_widget_->setMinimumSize(320, 240);
        preview_widget_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    });
    
    // Create line scope dialog
    line_scope_dialog_ = new LineScopeDialog(this);
    
    // Create field timing dialog
    field_timing_dialog_ = new FieldTimingDialog(this);
    
    // Connect to dialog hide/close events to disable cross-hairs
    connect(line_scope_dialog_, &QDialog::finished, this, [this]() {
        preview_widget_->setCrosshairsEnabled(false);
    });
    connect(line_scope_dialog_, &QDialog::rejected, this, [this]() {
        preview_widget_->setCrosshairsEnabled(false);
    });
    
    // Connect line clicked signal
    connect(preview_widget_, &FieldPreviewWidget::lineClicked, [this](int image_x, int image_y) {
        emit lineScopeRequested(image_x, image_y);
    });
}

void PreviewDialog::setCurrentNode(const QString& node_label, const QString& node_id)
{
    status_bar_->showMessage(QString("Viewing output from stage: %1").arg(node_id));
}

void PreviewDialog::onSampleMarkerMoved(int sample_x)
{
    // Emit signal for MainWindow to update cross-hairs
    // MainWindow has the context to map sample_x properly
    emit sampleMarkerMovedInLineScope(sample_x);
}
void PreviewDialog::closeChildDialogs()
{
    // Close line scope dialog if open
    if (line_scope_dialog_ && line_scope_dialog_->isVisible()) {
        line_scope_dialog_->close();
    }
    
    // Close field timing dialog if open
    if (field_timing_dialog_ && field_timing_dialog_->isVisible()) {
        field_timing_dialog_->close();
    }
    
    // Disable cross-hairs when closing
    if (preview_widget_) {
        preview_widget_->setCrosshairsEnabled(false);
    }
}

bool PreviewDialog::isLineScopeVisible() const
{
    return line_scope_dialog_ && line_scope_dialog_->isVisible();
}
void PreviewDialog::showLineScope(const QString& node_id, int stage_index, uint64_t field_index, int line_number, int sample_x, 
                                  const std::vector<uint16_t>& samples,
                                  const std::optional<orc::presenters::VideoParametersView>& video_params,
                                   int preview_image_width, int original_sample_x, int original_image_y,
                                  orc::PreviewOutputType preview_mode,
                                  const std::vector<uint16_t>& y_samples,
                                  const std::vector<uint16_t>& c_samples)
{
    if (line_scope_dialog_) {
        // Store line scope context for cross-hair updates
        current_line_scope_preview_width_ = preview_image_width;
        current_line_scope_samples_count_ = samples.size();
        if (current_line_scope_samples_count_ == 0 && !y_samples.empty()) {
            // Use Y samples size if no composite
            current_line_scope_samples_count_ = y_samples.size();
        }
        // Note: We don't know the image_y here directly, but MainWindow will update cross-hairs
        
        // Connect navigation signal if not already connected
        connect(line_scope_dialog_, &LineScopeDialog::lineNavigationRequested,
                this, &PreviewDialog::lineNavigationRequested, Qt::UniqueConnection);
        
        // Connect refresh signal if not already connected
        connect(line_scope_dialog_, &LineScopeDialog::refreshRequested,
                this, &PreviewDialog::lineScopeRequested, Qt::UniqueConnection);
        
        // Connect sample marker moved signal to update cross-hairs
        connect(line_scope_dialog_, &LineScopeDialog::sampleMarkerMoved,
                this, &PreviewDialog::onSampleMarkerMoved, Qt::UniqueConnection);
        
        // Only enable cross-hairs if there's actual data to display
        // For stages like FFmpeg video sync that don't have line data, hide cross-hairs
        if (samples.empty() && y_samples.empty() && c_samples.empty()) {
            preview_widget_->setCrosshairsEnabled(false);
        } else {
            preview_widget_->setCrosshairsEnabled(true);
        }
        
        line_scope_dialog_->setLineSamples(node_id, stage_index, field_index, line_number, sample_x, samples, video_params, 
                                          preview_image_width, original_sample_x, original_image_y, preview_mode,
                                          y_samples, c_samples);
        
        // Only show if not already visible to avoid position resets
        if (!line_scope_dialog_->isVisible()) {
            line_scope_dialog_->show();
        }
    }
}
void PreviewDialog::notifyFrameChanged()
{
    emit previewFrameChanged();
}