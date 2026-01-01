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
#include <QPushButton>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QString>
#include "../core/include/preview_renderer.h"

class FieldPreviewWidget;

/**
 * @brief Separate dialog window for previewing field/frame outputs from DAG nodes
 * 
 * Provides a dedicated window for viewing video field/frame previews with controls for:
 * - Field/frame navigation via slider
 * - Preview mode selection (field, frame, split, etc.)
 * - Aspect ratio control
 * - Export to PNG
 * - VBI and other metadata dialogs
 * 
 * This is a thin GUI layer - all rendering logic is handled by orc::PreviewRenderer.
 */
class PreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreviewDialog(QWidget *parent = nullptr);
    ~PreviewDialog();
    
    /// @name Widget Accessors
    /// @{
    FieldPreviewWidget* previewWidget() { return preview_widget_; }  ///< Get preview widget
    QSlider* previewSlider() { return preview_slider_; }  ///< Get field/frame slider
    QLabel* previewInfoLabel() { return preview_info_label_; }  ///< Get info label
    QLabel* sliderMinLabel() { return slider_min_label_; }  ///< Get slider min label
    QLabel* sliderMaxLabel() { return slider_max_label_; }  ///< Get slider max label
    QComboBox* previewModeCombo() { return preview_mode_combo_; }  ///< Get preview mode selector
    QComboBox* aspectRatioCombo() { return aspect_ratio_combo_; }  ///< Get aspect ratio selector
    QAction* pulldownAction() { return show_pulldown_action_; }  ///< Get pulldown menu action
    /// @}
    
    /**
     * @brief Set the currently previewed node
     * @param node_label Human-readable node label
     * @param node_id Node identifier string
     */
    void setCurrentNode(const QString& node_label, const QString& node_id);

Q_SIGNALS:
    void previewIndexChanged(int index);
    void sequentialPreviewRequested(int index);  // Emitted when next/prev button clicked
    void previewModeChanged(int index);
    void aspectRatioModeChanged(int index);
    void exportPNGRequested();
    void showVBIDialogRequested();  // Emitted when VBI Decoder menu item selected
    void showHintsDialogRequested();  // Emitted when Hints menu item selected
    void showQualityMetricsDialogRequested();  // Emitted when Quality Metrics menu item selected
    void showPulldownDialogRequested();  // Emitted when Pulldown Observer menu item selected
    void showDropoutsChanged(bool show);  // Emitted when dropout visibility changes

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
    QAction* show_vbi_action_;
    QAction* show_hints_action_;
    QAction* show_quality_metrics_action_;
    QAction* show_pulldown_action_;
    
    // Navigation buttons
    QPushButton* first_button_;
    QPushButton* prev_button_;
    QPushButton* next_button_;
    QPushButton* last_button_;
    QPushButton* zoom1to1_button_;
    QPushButton* dropouts_button_;
};

#endif // PREVIEWDIALOG_H
