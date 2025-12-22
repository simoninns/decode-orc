/*
 * File:        previewdialog.h
 * Module:      orc-gui
 * Purpose:     Separate preview window for field/frame viewing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <QDialog>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QString>
#include "../core/include/preview_renderer.h"

class FieldPreviewWidget;

/**
 * @brief Separate dialog window for previewing field/frame outputs from DAG nodes
 */
class PreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreviewDialog(QWidget *parent = nullptr);
    ~PreviewDialog();
    
    // Preview control access
    FieldPreviewWidget* previewWidget() { return preview_widget_; }
    QSlider* previewSlider() { return preview_slider_; }
    QLabel* previewInfoLabel() { return preview_info_label_; }
    QLabel* sliderMinLabel() { return slider_min_label_; }
    QLabel* sliderMaxLabel() { return slider_max_label_; }
    QComboBox* previewModeCombo() { return preview_mode_combo_; }
    QComboBox* aspectRatioCombo() { return aspect_ratio_combo_; }
    
    void setCurrentNode(const QString& node_label, const QString& node_id);

Q_SIGNALS:
    void previewIndexChanged(int index);
    void previewModeChanged(int index);
    void aspectRatioModeChanged(int index);
    void exportPNGRequested();

private:
    void setupUI();
    
    // UI components
    FieldPreviewWidget* preview_widget_;
    QSlider* preview_slider_;
    QLabel* preview_info_label_;
    QLabel* slider_min_label_;
    QLabel* slider_max_label_;
    QComboBox* preview_mode_combo_;
    QComboBox* aspect_ratio_combo_;
    QMenuBar* menu_bar_;
    QStatusBar* status_bar_;
    QAction* export_png_action_;
};

#endif // PREVIEWDIALOG_H
