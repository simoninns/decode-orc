/*
 * File:        previewdialog.cpp
 * Module:      orc-gui
 * Purpose:     Separate preview window for field/frame viewing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "previewdialog.h"
#include "fieldpreviewwidget.h"
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

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setWindowTitle("Field/Frame Preview");
    
    // Use Qt::Window flag to allow independent positioning (like ld-analyse dialogs)
    setWindowFlags(Qt::Window);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size - geometry will be restored by MainWindow
    resize(800, 700);
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
    
    // Set auto-repeat on prev/next buttons (150ms delay, then fast repeat)
    prev_button_->setAutoRepeat(true);
    prev_button_->setAutoRepeatDelay(150);
    prev_button_->setAutoRepeatInterval(50);
    
    next_button_->setAutoRepeat(true);
    next_button_->setAutoRepeatDelay(150);
    next_button_->setAutoRepeatInterval(50);
    
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
    
    controlLayout->addWidget(new QLabel("Aspect Ratio:"));
    aspect_ratio_combo_ = new QComboBox();
    controlLayout->addWidget(aspect_ratio_combo_);
    
    controlLayout->addStretch();
    mainLayout->addLayout(controlLayout);
    
    // Status bar
    status_bar_ = new QStatusBar(this);
    status_bar_->showMessage("No node selected");
    mainLayout->addWidget(status_bar_);
    
    // Connect signals
    connect(preview_slider_, &QSlider::valueChanged, this, &PreviewDialog::previewIndexChanged);
    connect(preview_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::previewModeChanged);
    connect(aspect_ratio_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::aspectRatioModeChanged);
    
    // Connect navigation buttons
    connect(first_button_, &QPushButton::clicked, [this]() {
        int new_value = preview_slider_->minimum();
        preview_slider_->setValue(new_value);
        emit sequentialPreviewRequested(new_value);
    });
    connect(prev_button_, &QPushButton::clicked, [this]() {
        int new_value = preview_slider_->value() - 1;
        preview_slider_->setValue(new_value);
        emit sequentialPreviewRequested(new_value);
    });
    connect(next_button_, &QPushButton::clicked, [this]() {
        int new_value = preview_slider_->value() + 1;
        preview_slider_->setValue(new_value);
        emit sequentialPreviewRequested(new_value);
    });
    connect(last_button_, &QPushButton::clicked, [this]() {
        int new_value = preview_slider_->maximum();
        preview_slider_->setValue(new_value);
        emit sequentialPreviewRequested(new_value);
    });
}

void PreviewDialog::setCurrentNode(const QString& node_label, const QString& node_id)
{
    status_bar_->showMessage(QString("Viewing \"%1\" (%2)").arg(node_label, node_id));
}
