/*
 * File:        configdialogbase.h
 * Module:      orc-gui
 * Purpose:     Base class for simple rule-based configuration dialogs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef CONFIGDIALOGBASE_H
#define CONFIGDIALOGBASE_H

#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <map>
#include <string>
#include <functional>
#include <parameter_types.h>

/**
 * @brief Base class for simple rule-based configuration dialogs
 * 
 * This class provides a framework for creating user-friendly configuration
 * dialogs that map UI controls (checkboxes, combo boxes, etc.) to stage
 * parameters. Unlike AnalysisDialogBase (for data visualization) and
 * StageParameterDialog (for generic parameter editing), this is designed
 * for simple, intuitive configuration through rules-based mapping.
 * 
 * Key features:
 * - High-level UI controls (checkboxes, dropdowns) that map to parameters
 * - Rules that can set multiple parameters from a single UI control
 * - Preset/template support for common configurations
 * - Clear, user-friendly labels and descriptions
 * - No need for users to understand raw parameter values
 * 
 * Example use cases:
 * - "Mask NTSC subtitles" checkbox -> sets lineSpec="F:21", maskIRE=0
 * - "VBI removal mode" dropdown -> sets different line ranges per selection
 * - "Field selection" radio buttons -> sets field-related parameters
 * 
 * Derived classes should:
 * 1. Build their UI in the constructor using helper methods
 * 2. Implement apply_configuration() to map UI state to parameters
 * 3. Optionally implement load_from_parameters() to initialize UI from params
 */
class ConfigDialogBase : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param title Dialog window title
     * @param parent Parent widget
     */
    explicit ConfigDialogBase(const QString& title, QWidget *parent = nullptr);
    virtual ~ConfigDialogBase() = default;

    /**
     * @brief Get the parameter values configured by this dialog
     * @return Map of parameter names to values
     */
    std::map<std::string, orc::ParameterValue> get_parameters() const;

    /**
     * @brief Set parameter values and update UI accordingly
     * @param params Current parameter values to display
     */
    void set_parameters(const std::map<std::string, orc::ParameterValue>& params);

protected:
    /**
     * @brief Apply current UI state to parameter map
     * 
     * Derived classes must implement this to translate UI controls
     * into parameter values. This is called when the user clicks OK/Apply.
     */
    virtual void apply_configuration() = 0;

    /**
     * @brief Load parameter values into UI controls
     * 
     * Derived classes should implement this to reverse the apply_configuration()
     * process - setting UI controls based on parameter values.
     * 
     * @param params Parameter values to load into UI
     */
    virtual void load_from_parameters(const std::map<std::string, orc::ParameterValue>& params);

    /**
     * @brief Create a group box for organizing related controls
     * @param title Group box title
     * @return New group box with form layout ready for controls
     */
    QGroupBox* create_group(const QString& title);

    /**
     * @brief Add a checkbox control
     * @param layout Layout to add to
     * @param label Checkbox label text
     * @param tooltip Optional tooltip text
     * @return Created checkbox widget
     */
    QCheckBox* add_checkbox(QFormLayout* layout, const QString& label, 
                           const QString& tooltip = QString());

    /**
     * @brief Add a combo box (dropdown) control
     * @param layout Layout to add to
     * @param label Label text for the control
     * @param items List of items for the dropdown
     * @param tooltip Optional tooltip text
     * @return Created combo box widget
     */
    QComboBox* add_combobox(QFormLayout* layout, const QString& label,
                           const QStringList& items, const QString& tooltip = QString());

    /**
     * @brief Add an integer spin box control
     * @param layout Layout to add to
     * @param label Label text for the control
     * @param min Minimum value
     * @param max Maximum value
     * @param default_value Default value
     * @param tooltip Optional tooltip text
     * @return Created spin box widget
     */
    QSpinBox* add_spinbox(QFormLayout* layout, const QString& label,
                         int min, int max, int default_value,
                         const QString& tooltip = QString());

    /**
     * @brief Add a double spin box control
     * @param layout Layout to add to
     * @param label Label text for the control
     * @param min Minimum value
     * @param max Maximum value
     * @param default_value Default value
     * @param decimals Number of decimal places
     * @param tooltip Optional tooltip text
     * @return Created double spin box widget
     */
    QDoubleSpinBox* add_double_spinbox(QFormLayout* layout, const QString& label,
                                      double min, double max, double default_value,
                                      int decimals = 2, const QString& tooltip = QString());

    /**
     * @brief Add an informational label
     * @param layout Layout to add to
     * @param text Label text (can include HTML formatting)
     */
    void add_info_label(QFormLayout* layout, const QString& text);

    /**
     * @brief Set a parameter value in the internal map
     * @param name Parameter name
     * @param value Parameter value
     */
    void set_parameter(const std::string& name, const orc::ParameterValue& value);

    /**
     * @brief Get a parameter value from the internal map
     * @param name Parameter name
     * @param default_value Value to return if parameter not found
     * @return Parameter value or default
     */
    orc::ParameterValue get_parameter(const std::string& name, 
                                      const orc::ParameterValue& default_value = std::string("")) const;

    // Main layouts
    QVBoxLayout* main_layout_;
    QFormLayout* form_layout_;
    QDialogButtonBox* button_box_;

private slots:
    void on_accept();
    void on_reset();

private:
    virtual void reset_to_defaults();

    // Parameter storage
    std::map<std::string, orc::ParameterValue> parameters_;
};

#endif // CONFIGDIALOGBASE_H
