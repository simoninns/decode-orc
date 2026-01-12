/*
 * File:        previewdialog.h
 * Module:      orc-gui
 * Purpose:     Separate preview window for field/frame viewing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
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
#include <vector>
#include <cstdint>
#include <optional>
#include "../core/include/preview_renderer.h"
#include "../core/include/tbc_metadata.h"

class FieldPreviewWidget;
class LineScopeDialog;

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
    QComboBox* signalCombo() { return signal_combo_; }  ///< Get signal selector (Y/C/Y+C for YC sources)
    QLabel* signalLabel() { return signal_label_; }  ///< Get signal label
    QComboBox* aspectRatioCombo() { return aspect_ratio_combo_; }  ///< Get aspect ratio selector
    QAction* pulldownAction() { return show_pulldown_action_; }  ///< Get pulldown menu action
    QPushButton* dropoutsButton() { return dropouts_button_; }  ///< Get dropouts button for state control
    /// @}
    
    /**
     * @brief Set visibility of signal controls (label and combo box)
     * @param visible True to show signal controls, false to hide them
     */
    void setSignalControlsVisible(bool visible);
    
    /**
     * @brief Set the currently previewed node
     * @param node_label Human-readable node label
     * @param node_id Node identifier string
     */
    void setCurrentNode(const QString& node_label, const QString& node_id);
    
    /**
     * @brief Show line scope dialog with sample data
     * @param node_id Node identifier for the stage being viewed
     * @param field_index Field number being displayed
     * @param line_number Line number being displayed
     * @param sample_x Sample X position that was clicked
     * @param samples Vector of 16-bit samples for the line
     * @param video_params Optional video parameters for region markers
     * @param y_samples Optional Y channel samples for YC sources
     * @param c_samples Optional C channel samples for YC sources
     */
    void showLineScope(const QString& node_id, uint64_t field_index, int line_number, int sample_x, 
                       const std::vector<uint16_t>& samples,
                       const std::optional<orc::VideoParameters>& video_params,
                       int preview_image_width, int original_sample_x,
                       const std::vector<uint16_t>& y_samples = {},
                       const std::vector<uint16_t>& c_samples = {});
    
    /**
     * @brief Close all child dialogs (e.g., line scope)
     */
    void closeChildDialogs();
    
    /**
     * @brief Check if line scope dialog is currently visible
     */
    bool isLineScopeVisible() const;
    
    /**
     * @brief Get line scope dialog (for updating when stage changes)
     */
    LineScopeDialog* lineScopeDialog() { return line_scope_dialog_; }

Q_SIGNALS:
    void previewIndexChanged(int index);
    void sequentialPreviewRequested(int index);  // Emitted when next/prev button clicked
    void previewModeChanged(int index);
    void signalChanged(int index);  // Emitted when signal selection changes (Y/C/Y+C)
    void aspectRatioModeChanged(int index);
    void exportPNGRequested();
    void showVBIDialogRequested();  // Emitted when VBI Decoder menu item selected
    void showHintsDialogRequested();  // Emitted when Hints menu item selected
    void showQualityMetricsDialogRequested();  // Emitted when Quality Metrics menu item selected
    void showPulldownDialogRequested();  // Emitted when Pulldown Observer menu item selected
    void showDropoutsChanged(bool show);  // Emitted when dropout visibility changes
    void lineScopeRequested(int image_x, int image_y);  // Emitted when user clicks a line
    void lineNavigationRequested(int direction, uint64_t current_field, int current_line, int sample_x, int preview_image_width);  // Emitted when navigating lines
    void sampleMarkerMovedInLineScope(int sample_x);  // Emitted when sample marker moves in line scope

private slots:
    void onSampleMarkerMoved(int sample_x);

private:
    void setupUI();
    
    // UI components
    FieldPreviewWidget* preview_widget_;
    QSlider* preview_slider_;
    QLabel* preview_info_label_;
    QLabel* slider_min_label_;
    QLabel* slider_max_label_;
    QComboBox* preview_mode_combo_;
    QComboBox* signal_combo_;  // Signal selection for YC sources (Y/C/Y+C)
    QLabel* signal_label_;  // Label for signal combo box
    QComboBox* aspect_ratio_combo_;
    QMenuBar* menu_bar_;
    QStatusBar* status_bar_;
    QAction* export_png_action_;
    QAction* show_vbi_action_;
    QAction* show_hints_action_;
    QAction* show_quality_metrics_action_;
    QAction* show_pulldown_action_;
    LineScopeDialog* line_scope_dialog_;
    
    // Current line scope context for cross-hair updates
    int current_line_scope_line_ = -1;  // Image Y coordinate of current line being scoped
    int current_line_scope_preview_width_ = 0;
    int current_line_scope_samples_count_ = 0;
    
    // Navigation buttons
    QPushButton* first_button_;
    QPushButton* prev_button_;
    QPushButton* next_button_;
    QPushButton* last_button_;
    QPushButton* zoom1to1_button_;
    QPushButton* dropouts_button_;
};

#endif // PREVIEWDIALOG_H
