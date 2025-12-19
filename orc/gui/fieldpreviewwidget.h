/*
 * File:        fieldpreviewwidget.h
 * Module:      orc-gui
 * Purpose:     Field preview widget
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef FIELDPREVIEWWIDGET_H
#define FIELDPREVIEWWIDGET_H

#include <QWidget>
#include <QImage>
#include <memory>
#include <cstdint>

namespace orc {
    class VideoFieldRepresentation;
    class FieldID;
}

/**
 * Preview display modes
 */
enum class PreviewMode {
    SingleField,      // Display one field at a time
    Frame_EvenOdd,    // Weave fields: even on top (0+1)
    Frame_OddEven     // Weave fields: odd on top (1+0)
};

/**
 * Widget for displaying decoded video fields
 * 
 * Displays a single field at a time with optional:
 * - Scaling/zoom
 * - Deinterlacing visualization
 * - Dropout overlay
 */
class FieldPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FieldPreviewWidget(QWidget *parent = nullptr);
    ~FieldPreviewWidget();
    
    void setRepresentation(std::shared_ptr<const orc::VideoFieldRepresentation> repr);
    void setFieldIndex(uint64_t field_id);
    void setPreviewMode(PreviewMode mode);
    
    PreviewMode previewMode() const { return preview_mode_; }
    
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updatePreview();
    QImage renderField();
    QImage renderFrame();
    
    std::shared_ptr<const orc::VideoFieldRepresentation> representation_;
    uint64_t current_field_id_;
    PreviewMode preview_mode_;
    QImage preview_image_;
    bool needs_update_;
};

#endif // FIELDPREVIEWWIDGET_H
