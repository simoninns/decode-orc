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
#include <orc_rendering.h>  // For public API PreviewImage and DropoutRegion

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

void FieldPreviewWidget::setCrosshairsEnabled(bool enabled)
{
    crosshairs_enabled_ = enabled;
    if (!enabled) {
        // Clear locked state when disabling
        crosshairs_locked_ = false;
    }
    update();
}

void FieldPreviewWidget::updateCrosshairsPosition(int image_x, int image_y)
{
    if (current_image_.isNull()) {
        return;
    }
    
    // Clamp coordinates to valid image bounds
    QSize image_size = current_image_.size();
    image_x = qBound(0, image_x, image_size.width() - 1);
    image_y = qBound(0, image_y, image_size.height() - 1);
    
    // Map image coordinates to widget coordinates
    int widget_x = image_rect_.left() + (image_x * image_rect_.width()) / image_size.width();
    int widget_y = image_rect_.top() + (image_y * image_rect_.height()) / image_size.height();
    
    // Update locked cross-hairs position
    crosshairs_locked_ = true;
    locked_crosshairs_pos_ = QPoint(widget_x, widget_y);
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
    // Apply GUI-side aspect ratio correction by scaling width
    QRect target = rect();
    QSize image_size = current_image_.size();
    
    // Calculate display size with width correction (aspect_correction_)
    // aspect_correction_ = 1.0 for SAR 1:1; < 1.0 (e.g., ~0.7) for DAR 4:3
    QSize corrected_size(static_cast<int>(image_size.width() * aspect_correction_), image_size.height());
    QSize scaled_size = corrected_size.scaled(target.size(), Qt::KeepAspectRatio);
    
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
    
    // Draw dropout regions if enabled
    if (show_dropouts_ && !dropout_regions_.empty()) {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        
        // Draw each dropout region as a semi-transparent red rectangle
        for (const auto& region : dropout_regions_) {
            int line = static_cast<int>(region.line);
            int start_sample = static_cast<int>(region.start_sample);
            int end_sample = static_cast<int>(region.end_sample);
            
            // Validate region bounds
            if (line < 0 || line >= image_size.height() || 
                start_sample < 0 || end_sample > image_size.width() || 
                start_sample >= end_sample) {
                continue;
            }
            
            // Map image coordinates to widget coordinates
            qreal widget_x1 = image_rect_.left() + (start_sample * image_rect_.width()) / static_cast<qreal>(image_size.width());
            qreal widget_x2 = image_rect_.left() + (end_sample * image_rect_.width()) / static_cast<qreal>(image_size.width());
            qreal widget_y = image_rect_.top() + (line * image_rect_.height()) / static_cast<qreal>(image_size.height());
            
            // Calculate thickness - scale with image height, 1-4 pixels
            int thickness = std::max(1, std::min(4, image_rect_.height() / 200));
            
            // Draw solid red region centered on the scanline
            QColor color(255, 0, 0);  // Solid red
            QRectF dropout_rect(widget_x1, widget_y - thickness / 2.0, widget_x2 - widget_x1, thickness);
            painter.fillRect(dropout_rect, color);
        }
    }
    
    // Draw cross-hairs if enabled and (mouse is over the image area or cross-hairs are locked)
    QPoint draw_pos = crosshairs_locked_ ? locked_crosshairs_pos_ : mouse_pos_;
    if (crosshairs_enabled_ && (mouse_over_ || crosshairs_locked_) && image_rect_.contains(draw_pos)) {
        painter.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(Qt::green);
        pen.setWidth(1);
        painter.setPen(pen);
        
        // Map position to image pixel coordinates
        int img_x = ((draw_pos.x() - image_rect_.left()) * image_size.width()) / image_rect_.width();
        int img_y = ((draw_pos.y() - image_rect_.top()) * image_size.height()) / image_rect_.height();
        
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
    
    // If button is pressed and we're dragging, unlock cross-hairs
    if (mouse_button_pressed_) {
        crosshairs_locked_ = false;
    }
    
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
        
        // Lock cross-hairs at click position
        crosshairs_locked_ = true;
        locked_crosshairs_pos_ = event->pos();
        update();  // Redraw with locked cross-hairs
        
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
        
        // Lock cross-hairs at final position after drag
        if (image_rect_.contains(event->pos())) {
            crosshairs_locked_ = true;
            locked_crosshairs_pos_ = event->pos();
            update();
        }
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
    // Keep cross-hairs locked when mouse leaves - don't unlock them
    update();  // Trigger repaint
    QWidget::leaveEvent(event);
}

void FieldPreviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}
