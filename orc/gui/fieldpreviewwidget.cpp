// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "fieldpreviewwidget.h"
#include "video_field_representation.h"
#include "field_id.h"

#include <QPainter>
#include <QPaintEvent>
#include <algorithm>

FieldPreviewWidget::FieldPreviewWidget(QWidget *parent)
    : QWidget(parent)
    , current_field_id_(0)
    , preview_mode_(PreviewMode::SingleField)
    , needs_update_(false)
{
    setMinimumSize(320, 240);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

FieldPreviewWidget::~FieldPreviewWidget()
{
}

void FieldPreviewWidget::setRepresentation(
    std::shared_ptr<const orc::VideoFieldRepresentation> repr)
{
    representation_ = repr;
    needs_update_ = true;
    update();
}

void FieldPreviewWidget::setFieldIndex(uint64_t field_id)
{
    if (current_field_id_ != field_id) {
        current_field_id_ = field_id;
        needs_update_ = true;
        update();
    }
}

void FieldPreviewWidget::setPreviewMode(PreviewMode mode)
{
    if (preview_mode_ != mode) {
        preview_mode_ = mode;
        needs_update_ = true;
        update();
    }
}

QSize FieldPreviewWidget::sizeHint() const
{
    return QSize(768, 576); // PAL-ish aspect
}

void FieldPreviewWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    
    if (!representation_) {
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No TBC loaded");
        return;
    }
    
    if (needs_update_) {
        if (preview_mode_ == PreviewMode::SingleField) {
            preview_image_ = renderField();
        } else {
            preview_image_ = renderFrame();
        }
        needs_update_ = false;
    }
    
    if (preview_image_.isNull()) {
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "Field not available");
        return;
    }
    
    // Scale image to fit widget with corrected aspect ratio
    // TBC samples include blanking, so we need to adjust the aspect
    // Typical PAL: 4:3 display aspect, adjust width scaling
    QRect target = rect();
    QSize image_size = preview_image_.size();
    
    // Calculate proper display size with 4:3 aspect ratio
    // TBC width includes horizontal blanking, so we scale it down
    double aspect_correction = 0.7; // Empirical correction for TBC blanking
    QSize corrected_size(image_size.width() * aspect_correction, image_size.height());
    QSize scaled_size = corrected_size.scaled(target.size(), Qt::KeepAspectRatio);
    
    QRect dest_rect(
        (target.width() - scaled_size.width()) / 2,
        (target.height() - scaled_size.height()) / 2,
        scaled_size.width(),
        scaled_size.height()
    );
    
    painter.fillRect(rect(), Qt::black);
    painter.drawImage(dest_rect, preview_image_);
}

void FieldPreviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

void FieldPreviewWidget::updatePreview()
{
    needs_update_ = true;
    update();
}

QImage FieldPreviewWidget::renderField()
{
    if (!representation_) {
        return QImage();
    }
    
    orc::FieldID field_id(current_field_id_);
    
    if (!representation_->has_field(field_id)) {
        return QImage();
    }
    
    // Get field descriptor for dimensions
    auto desc_opt = representation_->get_descriptor(field_id);
    if (!desc_opt) {
        return QImage();
    }
    
    const auto& desc = *desc_opt;
    
    // Get field data
    auto field_data = representation_->get_field(field_id);
    if (field_data.empty()) {
        return QImage();
    }
    
    // Create QImage
    QImage image(desc.width, desc.height, QImage::Format_RGB888);
    
    // Convert 16-bit samples to 8-bit RGB
    // TBC samples are typically in range ~0-65535 with IRE levels around 16k-54k
    // We'll do a simple linear mapping for now
    
    for (size_t y = 0; y < desc.height; ++y) {
        auto* scan_line = image.scanLine(y);
        size_t offset = y * desc.width;
        
        for (size_t x = 0; x < desc.width && (offset + x) < field_data.size(); ++x) {
            uint16_t sample = field_data[offset + x];
            
            // Simple linear scaling from 16-bit to 8-bit
            // You may want to adjust this based on black/white levels from metadata
            uint8_t value = static_cast<uint8_t>((sample >> 8) & 0xFF);
            
            scan_line[x * 3 + 0] = value; // R
            scan_line[x * 3 + 1] = value; // G
            scan_line[x * 3 + 2] = value; // B
        }
    }
    
    return image;
}

QImage FieldPreviewWidget::renderFrame()
{
    if (!representation_) {
        return QImage();
    }
    
    // Determine which two fields to use based on preview mode
    uint64_t field_a_id, field_b_id;
    
    if (preview_mode_ == PreviewMode::Frame_EvenOdd) {
        // Even field first (field 0 on even lines, field 1 on odd lines)
        field_a_id = (current_field_id_ / 2) * 2;      // Even field
        field_b_id = field_a_id + 1;                   // Odd field
    } else { // Frame_OddEven
        // Odd field first (field 1 on even lines, field 0 on odd lines)
        field_a_id = (current_field_id_ / 2) * 2 + 1;  // Odd field
        field_b_id = field_a_id - 1;                   // Even field
    }
    
    orc::FieldID field_a(field_a_id);
    orc::FieldID field_b(field_b_id);
    
    if (!representation_->has_field(field_a) || !representation_->has_field(field_b)) {
        return renderField(); // Fall back to single field
    }
    
    // Get field descriptors
    auto desc_a_opt = representation_->get_descriptor(field_a);
    auto desc_b_opt = representation_->get_descriptor(field_b);
    
    if (!desc_a_opt || !desc_b_opt) {
        return renderField();
    }
    
    const auto& desc_a = *desc_a_opt;
    const auto& desc_b = *desc_b_opt;
    
    // Get field data
    auto field_a_data = representation_->get_field(field_a);
    auto field_b_data = representation_->get_field(field_b);
    
    if (field_a_data.empty() || field_b_data.empty()) {
        return renderField();
    }
    
    // Create frame image (double height)
    QImage image(desc_a.width, desc_a.height * 2, QImage::Format_RGB888);
    
    // Weave fields together
    for (size_t y = 0; y < desc_a.height * 2; ++y) {
        auto* scan_line = image.scanLine(y);
        
        // Alternate between fields
        bool use_field_a = (y % 2 == 0);
        const auto& field_data = use_field_a ? field_a_data : field_b_data;
        size_t field_line = y / 2;
        size_t offset = field_line * desc_a.width;
        
        for (size_t x = 0; x < desc_a.width && (offset + x) < field_data.size(); ++x) {
            uint16_t sample = field_data[offset + x];
            uint8_t value = static_cast<uint8_t>((sample >> 8) & 0xFF);
            
            scan_line[x * 3 + 0] = value; // R
            scan_line[x * 3 + 1] = value; // G
            scan_line[x * 3 + 2] = value; // B
        }
    }
    
    return image;
}
