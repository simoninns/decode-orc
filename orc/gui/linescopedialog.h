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
#include <vector>
#include <cstdint>
#include <optional>
#include "plotwidget.h"
#include "tbc_metadata.h"

/**
 * @brief Dialog for displaying line scope - all samples in a selected line
 * 
 * Shows a graph of all 16-bit sample values across a horizontal line
 * from the field/frame data.
 */
class LineScopeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LineScopeDialog(QWidget *parent = nullptr);
    ~LineScopeDialog();

    /**
     * @brief Display line samples
     * @param field_index The field number being displayed
     * @param line_number The line number being displayed
     * @param sample_x Sample X position that was clicked
     * @param samples Vector of 16-bit sample values for the line
     * @param video_params Optional video parameters for region markers
     */
    void setLineSamples(uint64_t field_index, int line_number, int sample_x, 
                        const std::vector<uint16_t>& samples,
                        const std::optional<orc::VideoParameters>& video_params,
                        int preview_image_width, int original_sample_x);

Q_SIGNALS:
    void lineNavigationRequested(int direction, uint64_t current_field, int current_line, int sample_x, int preview_image_width);

private slots:
    void onLineUp();
    void onLineDown();

private:
    void setupUI();
    
    PlotWidget* plot_widget_;
    PlotSeries* line_series_;
    QPushButton* line_up_button_;
    QPushButton* line_down_button_;
    
    // Current line info for navigation
    uint64_t current_field_index_;
    int current_line_number_;
    int current_sample_x_;  // Mapped field-space coordinate for display
    int original_sample_x_;  // Original preview-space coordinate for navigation
    int preview_image_width_;
};

#endif // LINESCOPEDIALOG_H
