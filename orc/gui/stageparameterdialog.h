/*
 * File:        stageparameterdialog.h
 * Module:      orc-gui
 * Purpose:     Stage parameter dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <orc/stage/params/parameter_types.h>
#include <stage_ux_strings.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "stage_parameter_context.h"

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
  // display label distinct from the stored value. The combo box shows the label
  // but stores the value; entries without the separator behave as before
  // (display == value). Single-sourced with the CLI's `allowed values` field so
  // the two front ends read an entry the same way.
  static constexpr char kComboValueLabelSeparator =
      orc::stage_ux::kComboValueLabelSeparator;

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

  /**
   * @brief Re-seed the dialog from a freshly derived context
   *
   * The dialog is modeless, so the graph it describes can be rewired while it
   * is open: an audio stage can gain or lose the input whose channel pairs its
   * dropdown lists, a Source Join can gain a connection its header should
   * name, a Video Parameters node can be moved to a source with different
   * metadata behind its reset button. This replaces all of that in place.
   *
   * Work in progress is never discarded: where the form holds edits that have
   * not been applied, those values are kept and only the surrounding context
   * changes. The form's widgets are rebuilt only when the descriptors
   * themselves differ; the common case of a refresh that changes nothing
   * structural leaves every widget, and its caret, alone.
   */
  void refresh_context(const orc::gui::StageParameterContext& context);

  /**
   * @brief Show values that were changed outside this dialog
   *
   * Another node's editor, an undo, or the recovery path that clears a
   * rejected node's parameters can all move the values under an open form.
   *
   * @return false when the form holds unapplied edits, in which case nothing
   *         is changed and the window title is marked so the user can see
   *         that what is on screen is no longer what the node holds
   */
  bool refresh_values(const std::map<std::string, orc::ParameterValue>& values);

  /**
   * @brief Whether the form differs from the values last applied from it
   */
  bool has_unapplied_edits() const;

  /**
   * @brief Name the node this dialog edits, in the window title
   *
   * Several parameter windows can be open at once, so each has to say which
   * node in the graph it belongs to. Called again when the stage is renamed.
   *
   * @param node_label Stage label as the graph draws it
   * @param node_id Node ID as the graph draws it
   */
  void set_node_identity(const QString& node_label, const QString& node_id);

  /**
   * @brief Ask for the window to open at a particular top-left corner
   *
   * Applied on first show, after the opening size has been worked out and
   * before the window is mapped, and clamped so the window lands on screen
   * whatever was asked for. Used to step successive editors apart so that a
   * second window does not open exactly on top of the first.
   */
  void set_opening_position(const QPoint& top_left);

  /**
   * @brief Whether the live-update checkbox is ticked
   *
   * Exposed so a caller can ask which mode the dialog is in rather than
   * inferring it from which signal an apply arrived on.
   */
  bool is_live_update_enabled() const;

  /**
   * @brief Everything wrong with the values currently in the form
   *
   * Value-level validation with no user interaction: the indexed frame/line
   * specs parse, and the pairs that must stay in order do
   * (orc::gui::crossParameterErrors). validate_values() reports these in a
   * message box when the user presses Update or OK, and the live-update path
   * uses them to decide whether the edit in progress is worth applying yet.
   *
   * Public so the rules the dialog applies can be read back directly, rather
   * than only through the modal they would otherwise be seen in.
   *
   * @return One line per problem; empty when the form is consistent
   */
  QStringList collect_validation_errors() const;

 protected:
  void showEvent(QShowEvent* event) override;

 signals:
  // Emitted when the user presses Update: apply the current values and report
  // any failure to the user.
  void update_requested();

  // Emitted while live update is ticked, once the edits have settled. The
  // caller applies the values the same way but must not interrupt editing
  // with modal errors — a half-finished edit is a normal transient state.
  void live_update_requested();

 private slots:
  void on_reset_defaults();
  void on_validate_and_accept();
  void on_validate_and_update();
  // Runs on every parameter edit: refreshes dependent widgets and, with live
  // update ticked, restarts the settle timer.
  void on_parameter_changed();
  void on_live_update_toggled(bool enabled);
  void on_live_update_timeout();

 private:
  QFormLayout* form_layout_;
  QScrollArea* scroll_area_;
  QDialogButtonBox* button_box_;
  QPushButton* reset_button_;
  QCheckBox* live_update_check_;
  QWidget* button_row_;
  // Always built, shown only when there is a description to show: the text is
  // part of the context and a refresh can give a stage a note it did not open
  // with (or take one away).
  QLabel* description_label_;

  // Coalesces a burst of edits (typing into a spin box, holding an arrow key)
  // into one apply: it restarts on every change and fires once the user
  // pauses, so a preview render is not started per keystroke.
  QTimer* live_update_timer_;

  // Values carried by the last apply, whether live, by Update or on opening,
  // so an edit that lands back on the values already applied does not
  // re-render — and so an external refresh can tell edits in progress from a
  // form that is simply showing what the node holds.
  std::optional<std::map<std::string, orc::ParameterValue>>
      last_applied_values_;

  std::string stage_name_;  // Stage name for QSettings keys
  QString project_path_;    // Project file path for relative path conversion
  std::optional<std::map<std::string, orc::ParameterValue>> reset_values_;

  // Window title parts. Held rather than composed once, because a rename, a
  // declined refresh or a live-update toggle each change one of them.
  QString display_name_;
  QString node_label_;
  QString node_id_text_;
  // Set when a refresh was declined because the form held unapplied edits:
  // the values on screen are then no longer the node's, and the title says so.
  bool refresh_declined_ = false;

  // Parameter descriptors (keep for validation and defaults)
  std::vector<orc::ParameterDescriptor> descriptors_;

  // Widgets for each parameter (indexed by parameter name)
  struct ParameterWidget {
    orc::ParameterType type;
    // The field occupying the form row. Usually the editor itself; a bounded
    // numeric parameter puts the editor in a row beside a slider, and a file
    // path beside its Browse button, in which case this is that container.
    // Showing, hiding and enabling act on this.
    QWidget* widget;
    // The control holding the value (QSpinBox, QCheckBox, QComboBox, ...).
    // Reading and writing the value act on this.
    QWidget* editor;
    QLabel* label;  // Associated label widget (for enabling/disabling)
  };
  std::map<std::string, ParameterWidget> parameter_widgets_;

  // Display text loaded into indexed spec editors (frame/line ranges). Used
  // to pass unrecognised legacy values through untouched: validation only
  // rejects a spec the user has actually modified.
  std::map<std::string, std::string> spec_display_baseline_;

  // Chooses the size the dialog opens at. Qt's own choice caps the height at
  // two thirds of the screen and takes the width from the widest widget's
  // hint, which squashes long parameter lists and leaves path fields too
  // narrow to read; this opens at what the form asks for, widened for text
  // entry and bounded by the screen.
  void apply_opening_size();
  bool opening_size_applied_ = false;

  // Where the window was asked to open, if anywhere. Applied once, with the
  // opening size, so the clamp knows how big the window will actually be.
  std::optional<QPoint> opening_position_;
  void apply_opening_position();

  // Build UI from descriptors
  void build_ui(
      const std::map<std::string, orc::ParameterValue>& current_values);

  // Throws the form away and builds it again from the current descriptors.
  // Only reached from refresh_context(), and only when the descriptors have
  // actually changed shape.
  void rebuild_form(
      const std::map<std::string, orc::ParameterValue>& current_values);

  // Sets the description shown above the form, hiding the label when there is
  // nothing to say.
  void set_stage_description(const std::string& description);

  // Composes the window title from the stage, the node it belongs to and
  // whether a refresh has been declined.
  void update_window_title();

  // Update widget enable/disable state based on dependencies
  void update_dependencies();

  // Puts a bounded numeric editor in a row with a slider covering the same
  // range, so a value judged by eye can be swept rather than typed; the two
  // controls track each other and the editor stays the value's home. Returns
  // the row to place in the form. Unbounded (or absurdly wide) parameters get
  // no slider and their editor is returned unchanged.
  QWidget* with_slider(QSpinBox* spin);
  QWidget* with_slider(QDoubleSpinBox* spin);

  // Guards the two-way link between a slider and its editor: the editor must
  // still report the change (dependencies, live update), so the reverse leg
  // is skipped by hand rather than by blocking the editor's signals.
  bool slider_sync_in_progress_ = false;

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
