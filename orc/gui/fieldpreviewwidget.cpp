/*
 * File:        fieldpreviewwidget.cpp
 * Module:      orc-gui
 * Purpose:     Field preview widget
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "fieldpreviewwidget.h"
#include "logging.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPen>
#include <cstring>

FieldPreviewWidget::FieldPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);  // Enable mouse tracking for cross-hairs
    
    // Setup line scope update throttling timer
    line_scope_update_timer_ = new QTimer(this);
    line_scope_update_timer_->setSingleShot(true);
    line_scope_update_timer_->setInterval(100);  // 100ms throttle
    connect(line_scope_update_timer_, &QTimer::timeout, this, &FieldPreviewWidget::onLineScopeUpdateTimer);
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
    
    // Scale image to fit widget
    // Note: Aspect ratio correction is now applied by orc-core in render_output,
    // so we just display the image as-is
    QRect target = rect();
    QSize image_size = current_image_.size();
    
    // Calculate proper display size
    QSize scaled_size = image_size.scaled(target.size(), Qt::KeepAspectRatio);
    
    QRect dest_rect(
        (target.width() - scaled_size.width()) / 2,
        (target.height() - scaled_size.height()) / 2,
        scaled_size.width(),
        scaled_size.height()
    );
    
    // Store image rect for cross-hair calculations
    image_rect_ = dest_rect;
    
    // Use fast transformation for better performance during scrubbing
    // Note: Qt::SmoothTransformation is slow, Qt::FastTransformation is much faster
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(dest_rect, current_image_);
    
    // Draw cross-hairs if mouse is over the image area
    if (mouse_over_ && image_rect_.contains(mouse_pos_)) {
        painter.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(Qt::green);
        pen.setWidth(1);
        painter.setPen(pen);
        
        // Map mouse position to image pixel coordinates
        int img_x = ((mouse_pos_.x() - image_rect_.left()) * image_size.width()) / image_rect_.width();
        int img_y = ((mouse_pos_.y() - image_rect_.top()) * image_size.height()) / image_rect_.height();
        
        // Clamp to image bounds
        img_x = qBound(0, img_x, image_size.width() - 1);
        img_y = qBound(0, img_y, image_size.height() - 1);
        
        // Map image pixel back to widget coordinates to get pixel-aligned positions
        // This ensures the cross-hairs align with the actual pixel boundaries
        qreal widget_x = image_rect_.left() + (img_x * image_rect_.width()) / static_cast<qreal>(image_size.width());
        qreal widget_y = image_rect_.top() + (img_y * image_rect_.height()) / static_cast<qreal>(image_size.height());
        qreal widget_x_end = image_rect_.left() + ((img_x + 1) * image_rect_.width()) / static_cast<qreal>(image_size.width());
        qreal widget_y_end = image_rect_.top() + ((img_y + 1) * image_rect_.height()) / static_cast<qreal>(image_size.height());
        
        // Calculate center of the pixel
        qreal center_x = (widget_x + widget_x_end) / 2.0;
        qreal center_y = (widget_y + widget_y_end) / 2.0;
        
        // Draw vertical line through pixel center
        painter.drawLine(QPointF(center_x, image_rect_.top()), QPointF(center_x, image_rect_.bottom()));
        
        // Draw horizontal line through pixel center
        painter.drawLine(QPointF(image_rect_.left(), center_y), QPointF(image_rect_.right(), center_y));
    }
}

void FieldPreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    mouse_pos_ = event->pos();
    mouse_over_ = true;
    update();  // Trigger repaint to show cross-hairs at new position
    
    // If mouse button is pressed and we're over the image, request line scope update
    if (mouse_button_pressed_ && image_rect_.contains(event->pos()) && !current_image_.isNull()) {
        // Throttle updates using timer
        pending_line_scope_pos_ = event->pos();
        line_scope_update_pending_ = true;
        
        if (!line_scope_update_timer_->isActive()) {
            // Fire immediately for first update, then throttle
            onLineScopeUpdateTimer();
            line_scope_update_timer_->start();
        }
    }
}

void FieldPreviewWidget::mousePressEvent(QMouseEvent *event)
{
    // Track mouse button state for drag detection
    if (event->button() == Qt::LeftButton) {
        mouse_button_pressed_ = true;
        
        // Emit signal for initial click if over the image area
        if (image_rect_.contains(event->pos()) && !current_image_.isNull()) {
            QSize image_size = current_image_.size();
            
            // Map mouse position to image pixel coordinates
            int img_x = ((event->pos().x() - image_rect_.left()) * image_size.width()) / image_rect_.width();
            int img_y = ((event->pos().y() - image_rect_.top()) * image_size.height()) / image_rect_.height();
            
            // Clamp to image bounds
            img_x = qBound(0, img_x, image_size.width() - 1);
            img_y = qBound(0, img_y, image_size.height() - 1);
            
            emit lineClicked(img_x, img_y);
        }
    }
    
    QWidget::mousePressEvent(event);
}

void FieldPreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        mouse_button_pressed_ = false;
        line_scope_update_timer_->stop();
        line_scope_update_pending_ = false;
    }
    
    QWidget::mouseReleaseEvent(event);
}

void FieldPreviewWidget::onLineScopeUpdateTimer()
{
    if (!line_scope_update_pending_ || current_image_.isNull()) {
        return;
    }
    
    line_scope_update_pending_ = false;
    
    QSize image_size = current_image_.size();
    
    // Map mouse position to image pixel coordinates
    int img_x = ((pending_line_scope_pos_.x() - image_rect_.left()) * image_size.width()) / image_rect_.width();
    int img_y = ((pending_line_scope_pos_.y() - image_rect_.top()) * image_size.height()) / image_rect_.height();
    
    // Clamp to image bounds
    img_x = qBound(0, img_x, image_size.width() - 1);
    img_y = qBound(0, img_y, image_size.height() - 1);
    
    emit lineClicked(img_x, img_y);
}

void FieldPreviewWidget::leaveEvent(QEvent *event)
{
    mouse_over_ = false;
    update();  // Trigger repaint to hide cross-hairs
    QWidget::leaveEvent(event);
}

void FieldPreviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}
