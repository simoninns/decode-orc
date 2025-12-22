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
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QCloseEvent>

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setWindowTitle("Field/Frame Preview");
    resize(800, 700);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
}

PreviewDialog::~PreviewDialog() = default;

void PreviewDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Toolbar for actions
    auto* toolbar = new QToolBar();
    export_png_action_ = toolbar->addAction("Export PNG");
    connect(export_png_action_, &QAction::triggered, this, &PreviewDialog::exportPNGRequested);
    mainLayout->addWidget(toolbar);
    
    // Current node display
    current_node_label_ = new QLabel("No node selected");
    current_node_label_->setStyleSheet("QLabel { font-weight: bold; padding: 5px; }");
    mainLayout->addWidget(current_node_label_);
    
    // Preview widget
    preview_widget_ = new FieldPreviewWidget(this);
    preview_widget_->setMinimumSize(640, 480);
    mainLayout->addWidget(preview_widget_, 1);
    
    // Preview info label
    preview_info_label_ = new QLabel("No preview available");
    mainLayout->addWidget(preview_info_label_);
    
    // Slider controls
    auto* sliderLayout = new QHBoxLayout();
    slider_min_label_ = new QLabel("0");
    slider_max_label_ = new QLabel("0");
    preview_slider_ = new QSlider(Qt::Horizontal);
    preview_slider_->setEnabled(false);
    
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
    
    // Connect signals
    connect(preview_slider_, &QSlider::valueChanged, this, &PreviewDialog::previewIndexChanged);
    connect(preview_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::previewModeChanged);
    connect(aspect_ratio_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::aspectRatioModeChanged);
}

void PreviewDialog::setCurrentNode(const QString& node_name)
{
    current_node_label_->setText(QString("Viewing node: %1").arg(node_name));
}

void PreviewDialog::closeEvent(QCloseEvent* event)
{
    // Just hide instead of closing
    event->ignore();
    hide();
}
