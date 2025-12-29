/*
 * File:        fieldpreviewwidget.cpp
 * Module:      orc-gui
 * Purpose:     Field preview widget
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "fieldpreviewwidget.h"
#include "logging.h"

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
        dropout_regions_.clear();
    } else {
        // Check if we need to reuse existing image buffer (avoid reallocation)
        if (current_image_.width() != static_cast<int>(image.width) ||
            current_image_.height() != static_cast<int>(image.height) ||
            current_image_.format() != QImage::Format_RGB888) {
            current_image_ = QImage(image.width, image.height, QImage::Format_RGB888);
        }
        
        // QImage aligns scanlines to 4-byte boundaries, so we need to check stride
        const int source_bytes_per_line = static_cast<int>(image.width * 3);
        const int qimage_bytes_per_line = current_image_.bytesPerLine();
        
        if (source_bytes_per_line == qimage_bytes_per_line) {
            // Fast path: strides match, bulk copy
            std::memcpy(current_image_.bits(), image.rgb_data.data(), image.rgb_data.size());
        } else {
            // Strides don't match due to alignment - copy line by line
            for (size_t y = 0; y < image.height; ++y) {
                auto* scan_line = current_image_.scanLine(y);
                const uint8_t* src = &image.rgb_data[y * source_bytes_per_line];
                std::memcpy(scan_line, src, source_bytes_per_line);
            }
        }
        
        // Store dropout regions for visualization
        dropout_regions_ = image.dropout_regions;
        ORC_LOG_DEBUG("FieldPreviewWidget::setImage - dropout regions count: {}", dropout_regions_.size());
    }
    
    update();
}

void FieldPreviewWidget::clearImage()
{
    current_image_ = QImage();
    dropout_regions_.clear();
    update();
}

void FieldPreviewWidget::setAspectCorrection(double correction)
{
    aspect_correction_ = correction;
    update();
}

void FieldPreviewWidget::setShowDropouts(bool show)
{
    show_dropouts_ = show;
    ORC_LOG_DEBUG("FieldPreviewWidget::setShowDropouts: {} (regions count: {})", show, dropout_regions_.size());
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
    
    // Use fast transformation for better performance during scrubbing
    // Note: Qt::SmoothTransformation is slow, Qt::FastTransformation is much faster
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(dest_rect, current_image_);
    
    // Draw dropout regions if enabled
    if (show_dropouts_ && !dropout_regions_.empty()) {
        ORC_LOG_DEBUG("Drawing {} dropout regions", dropout_regions_.size());
        
        // Calculate scaling factors from image coordinates to display coordinates
        double scale_x = static_cast<double>(scaled_size.width()) / image_size.width();
        double scale_y = static_cast<double>(scaled_size.height()) / image_size.height();
        
        ORC_LOG_DEBUG("Image size: {}x{}, Scaled size: {}x{}", image_size.width(), image_size.height(), 
                      scaled_size.width(), scaled_size.height());
        ORC_LOG_DEBUG("Scale factors - x: {}, y: {}", scale_x, scale_y);
        ORC_LOG_DEBUG("Dest rect: ({}, {}) {}x{}", dest_rect.left(), dest_rect.top(), 
                      dest_rect.width(), dest_rect.height());
        
        // Set up pen for dropout highlighting
        QPen dropout_pen(QColor(255, 0, 0, 200));  // Red with slight transparency
        dropout_pen.setWidth(2);
        painter.setPen(dropout_pen);
        painter.setRenderHint(QPainter::Antialiasing, true);
        
        // Draw each dropout region
        int count = 0;
        for (const auto& region : dropout_regions_) {
            // Convert line number to y coordinate (0-based)
            double y = region.line * scale_y + dest_rect.top();
            
            // Convert sample range to x coordinates
            double x1 = region.start_sample * scale_x + dest_rect.left();
            double x2 = region.end_sample * scale_x + dest_rect.left();
            
            if (count < 5) {  // Only log first 5 to avoid spam
                ORC_LOG_DEBUG("Dropout {}: line {} samples {}-{} -> y: {}, x: {}-{}", 
                              count, region.line, region.start_sample, region.end_sample, y, x1, x2);
            }
            
            // Draw horizontal line for the dropout region
            painter.drawLine(QPointF(x1, y), QPointF(x2, y));
            count++;
        }
    }
}

void FieldPreviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}
