/*
 * File:        fieldtimingdialog.h
 * Module:      orc-gui
 * Purpose:     Field timing visualization dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef FIELDTIMINGDIALOG_H
#define FIELDTIMINGDIALOG_H

#include <QDialog>
#include <QString>
#include <vector>
#include <cstdint>
#include <optional>
#include "presenters/include/hints_view_models.h"

class FieldTimingWidget;
class QPushButton;
class QSpinBox;
class QSlider;
class QLabel;

/**
 * @brief Dialog for viewing field samples as a timing graph
 * 
 * Displays field sample data as a graph with:
 * - Y-axis: sample value (0-65535 for 16-bit samples)
 * - X-axis: sample position (time)
 * 
 * The view shows one or two fields depending on preview mode and allows
 * horizontal scrolling to view the entire field data.
 */
class FieldTimingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FieldTimingDialog(QWidget *parent = nullptr);
    ~FieldTimingDialog();
    
    /**
     * @brief Set field data for timing display
     * 
     * @param node_id Node identifier for the stage being viewed
     * @param field_index Field number being displayed
     * @param samples Vector of 16-bit samples for the field
     * @param field_index_2 Optional second field index (for frame modes)
     * @param samples_2 Optional second field samples (for frame modes)
     * @param y_samples Optional Y channel samples for YC sources
     * @param c_samples Optional C channel samples for YC sources
     * @param y_samples_2 Optional Y channel samples for second field
     * @param c_samples_2 Optional C channel samples for second field
     * @param video_params Optional video parameters for mV conversion and level markers
     */
    void setFieldData(const QString& node_id, 
                     uint64_t field_index, 
                     const std::vector<uint16_t>& samples,
                     std::optional<uint64_t> field_index_2 = std::nullopt,
                     const std::vector<uint16_t>& samples_2 = {},
                     const std::vector<uint16_t>& y_samples = {},
                     const std::vector<uint16_t>& c_samples = {},
                     const std::vector<uint16_t>& y_samples_2 = {},
                     const std::vector<uint16_t>& c_samples_2 = {},
                     const std::optional<orc::presenters::VideoParametersView>& video_params = std::nullopt,
                     const std::optional<int>& marker_sample = std::nullopt);
    
    /**
     * @brief Get the timing widget
     */
    FieldTimingWidget* timingWidget() const { return timing_widget_; }

Q_SIGNALS:
    /**
     * @brief Emitted when user requests to refresh data for current position
     */
    void refreshRequested();
    
    /**
     * @brief Emitted when user wants to set crosshairs at center of timing view
     */
    void setCrosshairsRequested();

private:
    void setupUI();
    
    FieldTimingWidget* timing_widget_;
    QPushButton* jump_button_;
    QPushButton* set_crosshairs_button_;
    QSpinBox* line_spinbox_;
    QPushButton* jump_line_button_;
    QSlider* zoom_slider_;
    QLabel* zoom_value_label_;
    QString current_node_id_;
    uint64_t current_field_index_;
    std::optional<uint64_t> current_field_index_2_;
};

#endif // FIELDTIMINGDIALOG_H
