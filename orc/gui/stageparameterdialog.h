/*
 * File:        stageparameterdialog.h
 * Module:      orc-gui
 * Purpose:     Stage parameter dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <orc/stage/parameter_types.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Dialog for editing stage parameters
 *
 * Dynamically builds UI based on parameter descriptors from the stage.
 * Supports all parameter types: int32, uint32, double, bool, string
 */
class StageParameterDialog : public QDialog {
  Q_OBJECT

 public:
  // Separator used inside a string parameter's allowed_strings entry to carry a
  // display label distinct from the stored value: "value\x1flabel". The combo
  // box shows the label but stores the value. Entries without the separator
  // behave as before (display == value). '\x1f' (unit separator) cannot appear
  // in a normal parameter value, so this is collision-free.
  static constexpr char kComboValueLabelSeparator = '\x1f';

  /**
   * @brief Construct parameter editor dialog
   *
   * @param stage_name Internal stage identifier (e.g. "frame_map"); used for
   * QSettings keys and to identify parameters that need presentation
   * conversion (0-based stored values shown 1-based in the UI)
   * @param display_name Human-readable stage name (used for the window title)
   * @param stage_description Description of the stage (shown at the top of the
   * dialog)
   * @param descriptors Parameter descriptors from the stage
   * @param current_values Current parameter values
   * @param project_path Path to the project file (for relative path conversion)
   * @param reset_values Optional values used by the reset button (falls back to
   * descriptor defaults)
   * @param parent Parent widget
   */
  explicit StageParameterDialog(
      const std::string& stage_name, const std::string& display_name,
      const std::string& stage_description,
      const std::vector<orc::ParameterDescriptor>& descriptors,
      const std::map<std::string, orc::ParameterValue>& current_values,
      const QString& project_path = QString(),
      const std::optional<std::map<std::string, orc::ParameterValue>>&
          reset_values = std::nullopt,
      QWidget* parent = nullptr);

  /**
   * @brief Get updated parameter values from dialog
   *
   * @return Map of parameter names to new values
   */
  std::map<std::string, orc::ParameterValue> get_values() const;

 signals:
  void update_requested();

 private slots:
  void on_reset_defaults();
  void on_validate_and_accept();
  void on_validate_and_update();

 private:
  QFormLayout* form_layout_;
  QDialogButtonBox* button_box_;
  QPushButton* reset_button_;

  std::string stage_name_;  // Stage name for QSettings keys
  QString project_path_;    // Project file path for relative path conversion
  std::optional<std::map<std::string, orc::ParameterValue>> reset_values_;

  // Parameter descriptors (keep for validation and defaults)
  std::vector<orc::ParameterDescriptor> descriptors_;

  // Widgets for each parameter (indexed by parameter name)
  struct ParameterWidget {
    orc::ParameterType type;
    QWidget* widget;  // Points to actual widget (QSpinBox, QCheckBox, etc.)
    QLabel* label;    // Associated label widget (for enabling/disabling)
  };
  std::map<std::string, ParameterWidget> parameter_widgets_;

  // Display text loaded into indexed spec editors (frame/line ranges). Used
  // to pass unrecognised legacy values through untouched: validation only
  // rejects a spec the user has actually modified.
  std::map<std::string, std::string> spec_display_baseline_;

  // Build UI from descriptors
  void build_ui(
      const std::map<std::string, orc::ParameterValue>& current_values);

  // Update widget enable/disable state based on dependencies
  void update_dependencies();

  void reset_to_defaults();
  bool validate_values();

  // Helper to set widget value from ParameterValue
  void set_widget_value(const std::string& param_name,
                        const orc::ParameterValue& value);

  // Helper to get ParameterValue from widget
  orc::ParameterValue get_widget_value(const std::string& param_name) const;

  // Presentation conversion for indexed spec parameters (frame/line ranges):
  // stored values are 0-based, the UI shows 1-based numbers matching the
  // preview dialog. Non-spec parameters pass through unchanged.
  std::string to_display_spec(const std::string& param_name,
                              const std::string& stored_value) const;
  // Converts a displayed 1-based spec back to the stored 0-based form; falls
  // back to the raw text when it cannot be parsed (validate_values() reports
  // the error before the dialog can be accepted).
  std::string from_display_spec(const std::string& param_name,
                                const std::string& display_value) const;
};
