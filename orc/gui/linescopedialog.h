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
#include "tbc_metadata.h"

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
     * @param field_index The field number being displayed
     * @param line_number The line number being displayed
     * @param sample_x Sample X position that was clicked
     * @param samples Vector of 16-bit sample values for the line (will be converted to mV for display)
     * @param video_params Optional video parameters for IRE conversion and region markers
     * @param y_samples Optional Y channel samples for YC sources
     * @param c_samples Optional C channel samples for YC sources
     */
    void setLineSamples(const QString& node_id, uint64_t field_index, int line_number, int sample_x, 
                        const std::vector<uint16_t>& samples,
                        const std::optional<orc::VideoParameters>& video_params,
                        int preview_image_width, int original_sample_x,
                        const std::vector<uint16_t>& y_samples = {},
                        const std::vector<uint16_t>& c_samples = {});

Q_SIGNALS:
    void lineNavigationRequested(int direction, uint64_t current_field, int current_line, int sample_x, int preview_image_width);
    void sampleMarkerMoved(int sample_x);  // Emitted when sample marker position changes (field-space)

private slots:
    void onLineUp();
    void onLineDown();
    void onPlotClicked(const QPointF &dataPoint);
    void onChannelSelectionChanged(int index);

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
    int current_line_number_;
    int current_sample_x_;  // Mapped field-space coordinate for display
    int original_sample_x_;  // Original preview-space coordinate for navigation
    int preview_image_width_;
    std::vector<uint16_t> current_samples_;  // Store samples for marker updates (composite)
    std::vector<uint16_t> current_y_samples_;  // Store Y samples for YC sources
    std::vector<uint16_t> current_c_samples_;  // Store C samples for YC sources
    std::optional<orc::VideoParameters> current_video_params_;  // Store video params for IRE calc
    PlotMarker* sample_marker_;  // Green marker showing current sample position
    
    bool is_yc_source_;  // True if displaying YC source
};

#endif // LINESCOPEDIALOG_H
