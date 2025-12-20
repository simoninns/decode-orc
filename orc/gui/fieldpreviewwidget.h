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
#include "preview_renderer.h"

/**
 * Widget for displaying rendered previews from orc-core
 * 
 * This widget is now a thin display client - all rendering
 * logic is in orc::PreviewRenderer. The widget only:
 * - Displays RGB888 data from core
 * - Handles aspect ratio correction for display
 * - Manages widget sizing
 */
class FieldPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FieldPreviewWidget(QWidget *parent = nullptr);
    ~FieldPreviewWidget();
    
    /**
     * @brief Set the rendered image to display
     * @param image PreviewImage from orc::PreviewRenderer
     */
    void setImage(const orc::PreviewImage& image);
    
    /**
     * @brief Clear the display
     */
    void clearImage();
    
    /**
     * @brief Set the aspect ratio correction for display
     * @param correction Width scaling factor (1.0 = no correction, <1.0 = narrower)
     */
    void setAspectCorrection(double correction);
    
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QImage current_image_;
    double aspect_correction_ = 0.7;  // Default for PAL/NTSC DAR 4:3
};

#endif // FIELDPREVIEWWIDGET_H
