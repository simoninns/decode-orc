/*
 * File:        masklineconfigdialog.h
 * Module:      orc-gui
 * Purpose:     Configuration dialog for mask line stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef MASKLINECONFIGDIALOG_H
#define MASKLINECONFIGDIALOG_H

#include "configdialogbase.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>

/**
 * @brief Configuration dialog for the mask line stage
 * 
 * This dialog provides a user-friendly interface for configuring line masking
 * without requiring users to understand the raw line specification format.
 * 
 * Features:
 * - Common presets (NTSC closed captions, PAL teletext, etc.)
 * - Quick checkboxes for common masking scenarios
 * - Visual field/line selection
 * - IRE level control with presets (black, white, gray)
 * 
 * The dialog translates user-friendly selections into the lineSpec parameter
 * format expected by MaskLineStage.
 */
class MaskLineConfigDialog : public ConfigDialogBase
{
    Q_OBJECT

public:
    explicit MaskLineConfigDialog(QWidget *parent = nullptr);
    ~MaskLineConfigDialog() override = default;

protected:
    void apply_configuration() override;
    void load_from_parameters(const std::map<std::string, orc::ParameterValue>& params) override;

private slots:
    void on_preset_changed(int index);
    void on_ntsc_cc_changed(Qt::CheckState state);
    void on_custom_enabled_changed(Qt::CheckState state);
    void on_mask_level_preset_changed(int index);

private:
    void update_ui_state();
    void parse_line_spec_to_ui(const std::string& line_spec);
    std::string build_line_spec_from_ui() const;

    // Preset configuration group
    QComboBox* preset_combo_;

    // Quick options group
    QCheckBox* ntsc_cc_checkbox_;        // F:20 (NTSC closed captions, first field)
    QCheckBox* ntsc_vbi_checkbox_;       // F:10-20,S:10-20 (NTSC VBI area, both fields)

    // Custom configuration group
    QCheckBox* custom_enabled_checkbox_;
    QComboBox* field_selection_combo_;   // First/Second/Both fields
    QSpinBox* start_line_spinbox_;
    QSpinBox* end_line_spinbox_;

    // Mask level group
    QComboBox* mask_level_preset_combo_;  // Black/White/Gray/Custom
    QDoubleSpinBox* mask_ire_spinbox_;

    // State tracking
    bool updating_ui_;  // Flag to prevent recursive updates
};

#endif // MASKLINECONFIGDIALOG_H
