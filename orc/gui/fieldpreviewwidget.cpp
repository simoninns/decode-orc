/*
 * File:        fieldpreviewwidget.cpp
 * Module:      orc-gui
 * Purpose:     Field preview widget
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "fieldpreviewwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <cstring>

FieldPreviewWidget::FieldPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

FieldPreviewWidget::~FieldPreviewWidget()
{
}

void FieldPreviewWidget::setImage(const orc::PreviewImage& image)
{
    // Convert RGB888 data from orc-core to QImage
    if (image.rgb_data.empty() || image.width == 0 || image.height == 0) {
        current_image_ = QImage();
    } else {
        current_image_ = QImage(image.width, image.height, QImage::Format_RGB888);
        
        // Copy RGB data line by line
        for (size_t y = 0; y < image.height; ++y) {
            auto* scan_line = current_image_.scanLine(y);
            const uint8_t* src = &image.rgb_data[y * image.width * 3];
            std::memcpy(scan_line, src, image.width * 3);
        }
    }
    
    update();
}

void FieldPreviewWidget::clearImage()
{
    current_image_ = QImage();
    update();
}

void FieldPreviewWidget::setAspectCorrection(double correction)
{
    aspect_correction_ = correction;
    update();
}

QSize FieldPreviewWidget::sizeHint() const
{
    return QSize(768, 576); // PAL-ish aspect
}

void FieldPreviewWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    
    // Fill background
    painter.fillRect(rect(), palette().color(backgroundRole()));
    
    // Core always provides a renderable image (real content or placeholder)
    // so we don't need local "No preview available" handling
    if (current_image_.isNull()) {
        return;
    }
    
    // Scale image to fit widget with corrected aspect ratio
    // TBC samples include blanking, so we need to adjust the aspect
    // aspect_correction_ is set by the main window based on SAR/DAR mode
    QRect target = rect();
    QSize image_size = current_image_.size();
    
    // Calculate proper display size with aspect correction
    QSize corrected_size(image_size.width() * aspect_correction_, image_size.height());
    QSize scaled_size = corrected_size.scaled(target.size(), Qt::KeepAspectRatio);
    
    QRect dest_rect(
        (target.width() - scaled_size.width()) / 2,
        (target.height() - scaled_size.height()) / 2,
        scaled_size.width(),
        scaled_size.height()
    );
    
    painter.drawImage(dest_rect, current_image_);
}

void FieldPreviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}
