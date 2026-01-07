/*
 * File:        stageparameterdialog.h
 * Module:      orc-gui
 * Purpose:     Stage parameter dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#pragma once

#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <map>
#include <vector>
#include <string>
#include "stage_parameter.h"

/**
 * @brief Dialog for editing stage parameters
 * 
 * Dynamically builds UI based on parameter descriptors from the stage.
 * Supports all parameter types: int32, uint32, double, bool, string
 */
class StageParameterDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Construct parameter editor dialog
     * 
     * @param stage_name Name of the stage being edited
     * @param descriptors Parameter descriptors from the stage
     * @param current_values Current parameter values
     * @param parent Parent widget
     */
    explicit StageParameterDialog(
        const std::string& stage_name,
        const std::vector<orc::ParameterDescriptor>& descriptors,
        const std::map<std::string, orc::ParameterValue>& current_values,
        QWidget* parent = nullptr);
    
    /**
     * @brief Get updated parameter values from dialog
     * 
     * @return Map of parameter names to new values
     */
    std::map<std::string, orc::ParameterValue> get_values() const;

private slots:
    void on_reset_defaults();
    void on_validate_and_accept();

private:
    QFormLayout* form_layout_;
    QDialogButtonBox* button_box_;
    QPushButton* reset_button_;
    
    std::string stage_name_;  // Stage name for QSettings keys
    
    // Parameter descriptors (keep for validation and defaults)
    std::vector<orc::ParameterDescriptor> descriptors_;
    
    // Widgets for each parameter (indexed by parameter name)
    struct ParameterWidget {
        orc::ParameterType type;
        QWidget* widget;  // Points to actual widget (QSpinBox, QCheckBox, etc.)
        QLabel* label;    // Associated label widget (for enabling/disabling)
    };
    std::map<std::string, ParameterWidget> parameter_widgets_;
    
    // Build UI from descriptors
    void build_ui(const std::map<std::string, orc::ParameterValue>& current_values);
    
    // Update widget enable/disable state based on dependencies
    void update_dependencies();
    
    void reset_to_defaults();
    bool validate_values();
    
    // Helper to set widget value from ParameterValue
    void set_widget_value(const std::string& param_name, const orc::ParameterValue& value);
    
    // Helper to get ParameterValue from widget
    orc::ParameterValue get_widget_value(const std::string& param_name) const;
};
