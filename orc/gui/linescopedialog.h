/*
 * File:        linescopedialog.h
 * Module:      orc-gui
 * Purpose:     Line scope dialog for viewing line samples
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef LINESCOPEDIALOG_H
#define LINESCOPEDIALOG_H

#include <QDialog>
#include <QVector>
#include <QPointF>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <vector>
#include <cstdint>
#include <optional>
#include "plotwidget.h"
#include "presenters/include/hints_view_models.h"
#include <common_types.h>

/**
 * @brief Dialog for displaying line scope - all samples in a selected line
 * 
 * Shows a graph of sample values in millivolts (mV) across a horizontal line
 * from the field/frame data. Values are converted from 16-bit samples via IRE
 * using video system-specific conversion factors (PAL: 7 mV/IRE, NTSC: 7.143 mV/IRE).
 */
class LineScopeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LineScopeDialog(QWidget *parent = nullptr);
    ~LineScopeDialog();

    /**
     * @brief Display line samples
     * @param node_id Node identifier for the stage being viewed
     * @param field_index The field number being displayed (0-based)
     * @param line_number The line number being displayed (1-based for display, converted to 0-based internally)
     * @param sample_x Sample X position that was clicked
     * @param samples Vector of 16-bit sample values for the line (will be converted to mV for display)
     * @param video_params Optional video parameters for IRE conversion and region markers
     * @param preview_mode Current preview mode (Field/Frame/Split)
     * @param y_samples Optional Y channel samples for YC sources
     * @param c_samples Optional C channel samples for YC sources
     */
    void setLineSamples(const QString& node_id, uint64_t field_index, int line_number, int sample_x, 
                        const std::vector<uint16_t>& samples,
                        const std::optional<orc::presenters::VideoParametersView>& video_params,
                        int preview_image_width, int original_sample_x, int original_image_y,
                        orc::PreviewOutputType preview_mode,
                        const std::vector<uint16_t>& y_samples = {},
                        const std::vector<uint16_t>& c_samples = {});
    
    /// @name State Accessors
    /// @{
    uint64_t currentFieldIndex() const { return current_field_index_; }
    int currentLineNumber() const { return current_line_number_; }
    int currentSampleX() const { return current_sample_x_; }
    int previewImageWidth() const { return preview_image_width_; }
    /// @}
    
    /**
     * @brief Request refresh of current line samples
     * 
     * Emits a signal to re-request the current line data.
     * Used when the frame changes but the line scope should stay at the same position.
     */
    void refreshSamples();
    
    /**
     * @brief Refresh line samples at the current field/line that line scope is tracking
     * 
     * This is called when the preview frame changes. The line scope maintains its own
     * field/line position and this method ensures we get fresh samples from orc-core
     * for that position in the new frame context.
     * 
     * Emits lineNavigationRequested with current position to trigger sample fetch.
     * The key insight: line scope OWNS the state (current_field_index_, current_line_number_),
     * external events just trigger a refresh at that position.
     */
    void refreshSamplesAtCurrentPosition();

Q_SIGNALS:
    void lineNavigationRequested(int direction, uint64_t current_field, int current_line, int sample_x, int preview_image_width);
    void sampleMarkerMoved(int sample_x);  // Emitted when sample marker position changes (field-space)
    void refreshRequested(int image_x, int image_y);  // Emitted when refresh is needed (for frame changes)
    void dialogClosed();  // Emitted when dialog is closed/hidden

private slots:
    void onLineUp();
    void onLineDown();
    void onPlotClicked(const QPointF &dataPoint);
    void onChannelSelectionChanged(int index);

protected:
    void hideEvent(QHideEvent* event) override;

private:
    void setupUI();
    void updateSampleMarker(int sample_x);
    void updatePlotData();  // Redraw plot based on current channel selection
    
    PlotWidget* plot_widget_;
    PlotSeries* line_series_;
    PlotSeries* y_series_;  // Y channel series for YC sources
    PlotSeries* c_series_;  // C channel series for YC sources
    QPushButton* line_up_button_;
    QPushButton* line_down_button_;
    QLabel* sample_info_label_;
    QLabel* channel_selector_label_;  // Label for channel selector
    QComboBox* channel_selector_;  // Selector for Composite / Luma / Chroma / Both
    
    // Current line info for navigation
    QString current_node_id_;  // Node ID of the stage being viewed
    uint64_t current_field_index_;
    int current_line_number_;  // 0-based line number for internal use/navigation
    int current_sample_x_;  // Mapped field-space coordinate for display
    int original_sample_x_;  // Original preview-space X coordinate for navigation
    int original_image_y_;   // Original preview-space Y coordinate for refresh
    int preview_image_width_;
    orc::PreviewOutputType preview_mode_;  // Current preview mode (Field/Frame/Split)
    std::vector<uint16_t> current_samples_;  // Store samples for marker updates (composite)
    std::vector<uint16_t> current_y_samples_;  // Store Y samples for YC sources
    std::vector<uint16_t> current_c_samples_;  // Store C samples for YC sources
    std::optional<orc::presenters::VideoParametersView> current_video_params_;  // Store video params for IRE calc
    PlotMarker* sample_marker_;  // Green marker showing current sample position
    
    bool is_yc_source_;  // True if displaying YC source
};

#endif // LINESCOPEDIALOG_H
