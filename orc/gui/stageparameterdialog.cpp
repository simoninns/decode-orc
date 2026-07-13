/*
 * File:        stageparameterdialog.cpp
 * Module:      orc-gui
 * Purpose:     Stage parameter dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "stageparameterdialog.h"

#include <frame_numbering.h>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <utility>

#include "logging.h"

namespace {

// Splits an allowed_strings entry "value\x1flabel" into (value, label). With no
// separator the whole entry serves as both the stored value and the label.
std::pair<QString, QString> split_combo_item(const std::string& entry) {
  const auto pos = entry.find(StageParameterDialog::kComboValueLabelSeparator);
  if (pos == std::string::npos) {
    const QString both = QString::fromStdString(entry);
    return {both, both};
  }
  return {QString::fromStdString(entry.substr(0, pos)),
          QString::fromStdString(entry.substr(pos + 1))};
}

// Selects the combo entry whose stored data equals |value|; falls back to
// matching the visible text (for combos built without value/label separation).
void select_combo_value(QComboBox* combo, const QString& value) {
  const int idx = combo->findData(value);
  if (idx >= 0) {
    combo->setCurrentIndex(idx);
  } else {
    combo->setCurrentText(value);
  }
}

}  // namespace

StageParameterDialog::StageParameterDialog(
    const std::string& stage_name, const std::string& display_name,
    const std::string& stage_description,
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::map<std::string, orc::ParameterValue>& current_values,
    const QString& project_path,
    const std::optional<std::map<std::string, orc::ParameterValue>>&
        reset_values,
    QWidget* parent)
    : QDialog(parent),
      stage_name_(stage_name),
      descriptors_(descriptors),
      project_path_(project_path),
      reset_values_(reset_values) {
  setWindowTitle(
      QString("%1 Parameters").arg(QString::fromStdString(display_name)));
  setMinimumWidth(400);

  auto* main_layout = new QVBoxLayout(this);

  // Stage description label (shown at the top when non-empty)
  if (!stage_description.empty()) {
    auto* desc_label = new QLabel(QString::fromStdString(stage_description));
    desc_label->setWordWrap(true);
    desc_label->setStyleSheet(
        "color: palette(window-text); font-style: italic;");
    desc_label->setContentsMargins(0, 0, 0, 6);
    main_layout->addWidget(desc_label);
  }

  // Form layout for parameters
  form_layout_ = new QFormLayout();
  main_layout->addLayout(form_layout_);

  // Build UI based on descriptors
  build_ui(current_values);

  // Reset button uses metadata values when provided, otherwise descriptor
  // defaults.
  const bool has_custom_reset_values =
      reset_values_.has_value() && !reset_values_->empty();
  reset_button_ =
      new QPushButton(has_custom_reset_values ? "Reset to Metadata Values"
                                              : "Reset to Defaults");
  connect(reset_button_, &QPushButton::clicked, this,
          &StageParameterDialog::on_reset_defaults);

  // Dialog buttons
  button_box_ =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  auto* update_button =
      button_box_->addButton("Update", QDialogButtonBox::ApplyRole);
  connect(button_box_, &QDialogButtonBox::accepted, this,
          &StageParameterDialog::on_validate_and_accept);
  connect(button_box_, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(update_button, &QPushButton::clicked, this,
          &StageParameterDialog::on_validate_and_update);

  auto* button_layout = new QHBoxLayout();
  button_layout->addWidget(reset_button_);
  button_layout->addStretch();
  button_layout->addWidget(button_box_);

  main_layout->addLayout(button_layout);
}

void StageParameterDialog::build_ui(
    const std::map<std::string, orc::ParameterValue>& current_values) {
  for (const auto& desc : descriptors_) {
    QWidget* widget = nullptr;

    // Get current value or default
    orc::ParameterValue value;
    auto it = current_values.find(desc.name);
    if (it != current_values.end()) {
      value = it->second;
    } else if (desc.constraints.default_value.has_value()) {
      value = *desc.constraints.default_value;
    } else {
      // No default - use type-specific default
      switch (desc.type) {
        case orc::ParameterType::INT32:
          value = static_cast<int32_t>(0);
          break;
        case orc::ParameterType::UINT32:
          value = static_cast<uint32_t>(0);
          break;
        case orc::ParameterType::DOUBLE:
          value = 0.0;
          break;
        case orc::ParameterType::BOOL:
          value = false;
          break;
        case orc::ParameterType::STRING:
        case orc::ParameterType::FILE_PATH:
          value = std::string("");
          break;
      }
    }

    // Create appropriate widget based on type
    switch (desc.type) {
      case orc::ParameterType::INT32: {
        auto* spin = new QSpinBox();
        if (desc.constraints.min_value.has_value()) {
          spin->setMinimum(std::get<int32_t>(*desc.constraints.min_value));
        } else {
          spin->setMinimum(std::numeric_limits<int32_t>::min());
        }
        if (desc.constraints.max_value.has_value()) {
          spin->setMaximum(std::get<int32_t>(*desc.constraints.max_value));
        } else {
          spin->setMaximum(std::numeric_limits<int32_t>::max());
        }
        spin->setValue(std::get<int32_t>(value));

        widget = spin;
        break;
      }

      case orc::ParameterType::UINT32: {
        auto* spin = new QSpinBox();
        if (desc.constraints.min_value.has_value()) {
          spin->setMinimum(static_cast<int>(
              std::get<uint32_t>(*desc.constraints.min_value)));
        } else {
          spin->setMinimum(0);
        }
        if (desc.constraints.max_value.has_value()) {
          spin->setMaximum(static_cast<int>(
              std::get<uint32_t>(*desc.constraints.max_value)));
        } else {
          spin->setMaximum(std::numeric_limits<int>::max());
        }
        spin->setValue(static_cast<int>(std::get<uint32_t>(value)));
        widget = spin;
        break;
      }

      case orc::ParameterType::DOUBLE: {
        auto* spin = new QDoubleSpinBox();
        spin->setDecimals(4);
        // An unbounded default range makes QDoubleSpinBox size itself to the
        // width of numeric_limits::max() rendered in full (~300 digits), which
        // blows the dialog past the screen. Fall back to a large but finite
        // range when a descriptor leaves the bounds unset.
        constexpr double kDefaultDoubleBound = 1e12;
        if (desc.constraints.min_value.has_value()) {
          spin->setMinimum(std::get<double>(*desc.constraints.min_value));
        } else {
          spin->setMinimum(-kDefaultDoubleBound);
        }
        if (desc.constraints.max_value.has_value()) {
          spin->setMaximum(std::get<double>(*desc.constraints.max_value));
        } else {
          spin->setMaximum(kDefaultDoubleBound);
        }
        spin->setValue(std::get<double>(value));
        widget = spin;
        break;
      }

      case orc::ParameterType::BOOL: {
        auto* check = new QCheckBox();
        check->setChecked(std::get<bool>(value));
        widget = check;
        break;
      }

      case orc::ParameterType::STRING: {
        if (!desc.constraints.allowed_strings.empty()) {
          // Use combo box for constrained strings. Each entry may carry a
          // display label distinct from its stored value (see
          // kComboValueLabelSeparator).
          auto* combo = new QComboBox();
          for (const auto& allowed : desc.constraints.allowed_strings) {
            const auto [item_value, item_label] = split_combo_item(allowed);
            combo->addItem(item_label, item_value);
          }
          select_combo_value(
              combo, QString::fromStdString(std::get<std::string>(value)));
          widget = combo;
        } else {
          // Use line edit for free-form strings. Indexed spec parameters are
          // stored 0-based but presented 1-based.
          auto* edit = new QLineEdit();
          const std::string display_text =
              to_display_spec(desc.name, std::get<std::string>(value));
          edit->setText(QString::fromStdString(display_text));
          if (orc::indexed_spec_kind(stage_name_, desc.name) !=
              orc::IndexedSpecKind::kNone) {
            spec_display_baseline_[desc.name] = display_text;
          }
          widget = edit;
        }
        break;
      }

      case orc::ParameterType::FILE_PATH: {
        // File path with browse button
        auto* container = new QWidget();
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* edit = new QLineEdit();
        edit->setText(QString::fromStdString(std::get<std::string>(value)));
        edit->setObjectName("file_path_edit");

        auto* browse_btn = new QPushButton("Browse...");
        browse_btn->setObjectName("browse_button");

        // Capture stage_name and param name for determining dialog type
        std::string stage_name_copy = stage_name_;
        std::string param_name = desc.name;
        std::string display_name = desc.display_name;
        std::string file_ext_hint = desc.file_extension_hint;

        // Determine if this is an output path (save dialog) or input path (open
        // dialog). An explicit descriptor flag takes precedence; otherwise fall
        // back to a name heuristic (sink stages, "output" in the name).
        bool is_output = desc.output_path ||
                         (stage_name_copy.find("sink") != std::string::npos) ||
                         (param_name.find("output") != std::string::npos) ||
                         (display_name.find("Output") != std::string::npos);

        // Connect browse button to file dialog
        connect(
            browse_btn, &QPushButton::clicked,
            [this, edit, stage_name_copy, display_name, file_ext_hint,
             is_output]() {
              QSettings settings("orc-project", "orc-gui");
              QString settings_key =
                  QString("lastSourceDirectory/%1")
                      .arg(QString::fromStdString(stage_name_copy));

              // Get last directory for this source type
              QString last_dir =
                  settings.value(settings_key, QDir::homePath()).toString();

              // Use current path's directory if it exists, otherwise use
              // last_dir
              QString start_dir = last_dir;
              if (!edit->text().isEmpty()) {
                QFileInfo info(edit->text());
                if (info.exists() && info.dir().exists()) {
                  start_dir = info.dir().absolutePath();
                } else if (!edit->text().isEmpty()) {
                  // Path doesn't exist yet (output file) - use its directory if
                  // valid
                  QFileInfo parent_info(info.absolutePath());
                  if (parent_info.exists() && parent_info.isDir()) {
                    start_dir = parent_info.absolutePath();
                  }
                }
              }

              // Build file filter based on extension hint
              QString filter = "All Files (*)";
              QString dialog_title =
                  is_output ? "Select Output File" : "Select Input File";

              if (!file_ext_hint.empty()) {
                QString ext = QString::fromStdString(file_ext_hint);
                // Handle multiple extensions separated by | (e.g., ".rgb|.mp4")
                QStringList extensions = ext.split('|');
                QString ext_patterns;
                QString ext_names;

                for (const QString& e : extensions) {
                  QString trimmed = e.trimmed();
                  if (!ext_patterns.isEmpty()) {
                    ext_patterns += " ";
                    ext_names += "/";
                  }
                  ext_patterns += "*" + trimmed;
                  ext_names += trimmed.toUpper();
                }

                filter = ext_names.mid(1) + " Files (" + ext_patterns +
                         ");;All Files (*)";
                dialog_title =
                    is_output ? "Select Output " + ext_names.mid(1) + " File"
                              : "Select " + ext_names.mid(1) + " File";
              }

              QString file;
              if (is_output) {
                file = QFileDialog::getSaveFileName(this, dialog_title,
                                                    start_dir, filter);
              } else {
                file = QFileDialog::getOpenFileName(this, dialog_title,
                                                    start_dir, filter);
              }

              if (!file.isEmpty()) {
                // Convert to relative path if we have a project path
                QString path_to_store = file;
                if (!project_path_.isEmpty()) {
                  QDir project_dir(QFileInfo(project_path_).absolutePath());
                  path_to_store = project_dir.relativeFilePath(file);
                }
                edit->setText(path_to_store);
                // Save directory for this source type
                settings.setValue(settings_key, QFileInfo(file).absolutePath());
              }
            });

        // Special handling for input_path: auto-populate pcm_path and efm_path
        if (param_name == "input_path") {
          connect(edit, &QLineEdit::textChanged, [this, edit]() {
            QString tbc_path = edit->text();
            if (tbc_path.isEmpty()) return;

            // Get base path (remove .tbc extension if present)
            QString base_path = tbc_path;
            if (base_path.endsWith(".tbc", Qt::CaseInsensitive)) {
              base_path = base_path.left(base_path.length() - 4);
            }

            // Check for .pcm file
            auto pcm_it = parameter_widgets_.find("pcm_path");
            if (pcm_it != parameter_widgets_.end()) {
              QWidget* pcm_container = pcm_it->second.widget;
              QLineEdit* pcm_edit =
                  pcm_container->findChild<QLineEdit*>("file_path_edit");
              if (pcm_edit && pcm_edit->text().isEmpty()) {
                QString pcm_path = base_path + ".pcm";
                if (QFileInfo::exists(pcm_path)) {
                  pcm_edit->setText(pcm_path);
                }
              }
            }

            // Check for .efm file
            auto efm_it = parameter_widgets_.find("efm_path");
            if (efm_it != parameter_widgets_.end()) {
              QWidget* efm_container = efm_it->second.widget;
              QLineEdit* efm_edit =
                  efm_container->findChild<QLineEdit*>("file_path_edit");
              if (efm_edit && efm_edit->text().isEmpty()) {
                QString efm_path = base_path + ".efm";
                if (QFileInfo::exists(efm_path)) {
                  efm_edit->setText(efm_path);
                }
              }
            }

            // Check for .ac3sym file
            auto ac3rf_it = parameter_widgets_.find("ac3rf_path");
            if (ac3rf_it != parameter_widgets_.end()) {
              QWidget* ac3rf_container = ac3rf_it->second.widget;
              QLineEdit* ac3rf_edit =
                  ac3rf_container->findChild<QLineEdit*>("file_path_edit");
              if (ac3rf_edit && ac3rf_edit->text().isEmpty()) {
                QString ac3rf_path = base_path + ".ac3sym";
                if (QFileInfo::exists(ac3rf_path)) {
                  ac3rf_edit->setText(ac3rf_path);
                }
              }
            }
          });
        }

        // Special handling for YC source stages: auto-populate y_path/c_path
        // and pcm_path/efm_path
        if (param_name == "y_path" || param_name == "c_path") {
          connect(
              edit, &QLineEdit::textChanged,
              [this, edit, param_name, stage_name_copy]() {
                QString current_path = edit->text();
                if (current_path.isEmpty()) return;

                // Determine the extension pair and strip it to get the base
                // path. tbc_source uses .tbcy/.tbcc; CVBS source uses .y/.c.
                QString base_path = current_path;
                QString y_ext, c_ext;
                if (base_path.endsWith(".tbcy", Qt::CaseInsensitive) ||
                    base_path.endsWith(".tbcc", Qt::CaseInsensitive)) {
                  base_path = base_path.left(base_path.length() - 5);
                  y_ext = ".tbcy";
                  c_ext = ".tbcc";
                } else if (base_path.endsWith(".y", Qt::CaseInsensitive) ||
                           base_path.endsWith(".c", Qt::CaseInsensitive)) {
                  base_path = base_path.left(base_path.length() - 2);
                  y_ext = ".y";
                  c_ext = ".c";
                } else {
                  y_ext = ".tbcy";
                  c_ext = ".tbcc";
                }

                // Always sync the complementary YC file to match the new base
                // name
                if (param_name == "y_path") {
                  // Changing y_path: keep c_path in sync with the same base
                  // name
                  auto c_it = parameter_widgets_.find("c_path");
                  if (c_it != parameter_widgets_.end()) {
                    QWidget* c_container = c_it->second.widget;
                    QLineEdit* c_edit =
                        c_container->findChild<QLineEdit*>("file_path_edit");
                    if (c_edit) {
                      QSignalBlocker blocker(c_edit);
                      c_edit->setText(base_path + c_ext);
                    }
                  }
                } else if (param_name == "c_path") {
                  // Changing c_path: keep y_path in sync with the same base
                  // name
                  auto y_it = parameter_widgets_.find("y_path");
                  if (y_it != parameter_widgets_.end()) {
                    QWidget* y_container = y_it->second.widget;
                    QLineEdit* y_edit =
                        y_container->findChild<QLineEdit*>("file_path_edit");
                    if (y_edit) {
                      QSignalBlocker blocker(y_edit);
                      y_edit->setText(base_path + y_ext);
                    }
                  }
                }

                // Auto-populate pcm_path if not already set
                auto pcm_it = parameter_widgets_.find("pcm_path");
                if (pcm_it != parameter_widgets_.end()) {
                  QWidget* pcm_container = pcm_it->second.widget;
                  QLineEdit* pcm_edit =
                      pcm_container->findChild<QLineEdit*>("file_path_edit");
                  if (pcm_edit && pcm_edit->text().isEmpty()) {
                    QString pcm_path = base_path + ".pcm";
                    if (QFileInfo::exists(pcm_path)) {
                      pcm_edit->setText(pcm_path);
                    }
                  }
                }

                // Auto-populate efm_path if not already set
                auto efm_it = parameter_widgets_.find("efm_path");
                if (efm_it != parameter_widgets_.end()) {
                  QWidget* efm_container = efm_it->second.widget;
                  QLineEdit* efm_edit =
                      efm_container->findChild<QLineEdit*>("file_path_edit");
                  if (efm_edit && efm_edit->text().isEmpty()) {
                    QString efm_path = base_path + ".efm";
                    if (QFileInfo::exists(efm_path)) {
                      efm_edit->setText(efm_path);
                    }
                  }
                }

                // Auto-populate ac3rf_path if not already set
                auto ac3rf_it = parameter_widgets_.find("ac3rf_path");
                if (ac3rf_it != parameter_widgets_.end()) {
                  QWidget* ac3rf_container = ac3rf_it->second.widget;
                  QLineEdit* ac3rf_edit =
                      ac3rf_container->findChild<QLineEdit*>("file_path_edit");
                  if (ac3rf_edit && ac3rf_edit->text().isEmpty()) {
                    QString ac3rf_path = base_path + ".ac3sym";
                    if (QFileInfo::exists(ac3rf_path)) {
                      ac3rf_edit->setText(ac3rf_path);
                    }
                  }
                }

                // Auto-populate db_path if not already set
                auto db_it = parameter_widgets_.find("db_path");
                if (db_it != parameter_widgets_.end()) {
                  QWidget* db_container = db_it->second.widget;
                  QLineEdit* db_edit =
                      db_container->findChild<QLineEdit*>("file_path_edit");
                  if (db_edit && db_edit->text().isEmpty()) {
                    QString db_path = base_path + ".tbc.db";
                    if (QFileInfo::exists(db_path)) {
                      db_edit->setText(db_path);
                    }
                  }
                }
              });
        }

        layout->addWidget(edit, 1);  // Line edit takes most space
        layout->addWidget(browse_btn);

        widget = container;
        break;
      }
    }

    if (widget) {
      // Create label with description as tooltip
      auto* label = new QLabel(QString::fromStdString(desc.display_name) + ":");
      label->setToolTip(QString::fromStdString(desc.description));
      widget->setToolTip(QString::fromStdString(desc.description));

      form_layout_->addRow(label, widget);
      parameter_widgets_[desc.name] = ParameterWidget{desc.type, widget, label};

      // Connect change signals to update dependencies
      switch (desc.type) {
        case orc::ParameterType::STRING:
          if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &StageParameterDialog::update_dependencies);
          } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            connect(edit, &QLineEdit::textChanged, this,
                    &StageParameterDialog::update_dependencies);
          }
          break;
        case orc::ParameterType::INT32:
        case orc::ParameterType::UINT32:
          connect(static_cast<QSpinBox*>(widget),
                  QOverload<int>::of(&QSpinBox::valueChanged), this,
                  &StageParameterDialog::update_dependencies);
          break;
        case orc::ParameterType::DOUBLE:
          connect(static_cast<QDoubleSpinBox*>(widget),
                  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                  &StageParameterDialog::update_dependencies);
          break;
        case orc::ParameterType::BOOL:
          // Use stateChanged (Qt 6.0+) for compatibility with older Qt versions
          // checkStateChanged is only available in Qt 6.7+
          QT_WARNING_PUSH
          QT_WARNING_DISABLE_DEPRECATED
          connect(static_cast<QCheckBox*>(widget), &QCheckBox::stateChanged,
                  this, &StageParameterDialog::update_dependencies);
          QT_WARNING_POP
          break;
        default:
          break;
      }
    }
  }

  // Initial dependency update
  update_dependencies();

  // If no parameters, show message
  if (descriptors_.empty()) {
    form_layout_->addRow(
        new QLabel("This stage has no configurable parameters."));
    reset_button_->setEnabled(false);
  }
}

void StageParameterDialog::set_widget_value(const std::string& param_name,
                                            const orc::ParameterValue& value) {
  auto it = parameter_widgets_.find(param_name);
  if (it == parameter_widgets_.end()) return;

  const auto& pw = it->second;

  switch (pw.type) {
    case orc::ParameterType::INT32:
      static_cast<QSpinBox*>(pw.widget)->setValue(std::get<int32_t>(value));
      break;
    case orc::ParameterType::UINT32:
      static_cast<QSpinBox*>(pw.widget)->setValue(
          static_cast<int>(std::get<uint32_t>(value)));
      break;
    case orc::ParameterType::DOUBLE:
      static_cast<QDoubleSpinBox*>(pw.widget)->setValue(
          std::get<double>(value));
      break;
    case orc::ParameterType::BOOL:
      static_cast<QCheckBox*>(pw.widget)->setChecked(std::get<bool>(value));
      break;
    case orc::ParameterType::STRING:
      if (auto* combo = qobject_cast<QComboBox*>(pw.widget)) {
        select_combo_value(
            combo, QString::fromStdString(std::get<std::string>(value)));
      } else if (auto* edit = qobject_cast<QLineEdit*>(pw.widget)) {
        const std::string display_text =
            to_display_spec(param_name, std::get<std::string>(value));
        edit->setText(QString::fromStdString(display_text));
        if (orc::indexed_spec_kind(stage_name_, param_name) !=
            orc::IndexedSpecKind::kNone) {
          spec_display_baseline_[param_name] = display_text;
        }
      }
      break;
    case orc::ParameterType::FILE_PATH: {
      // For FILE_PATH, the widget is a container with a QLineEdit inside
      auto* edit = pw.widget->findChild<QLineEdit*>("file_path_edit");
      if (edit) {
        edit->setText(QString::fromStdString(std::get<std::string>(value)));
      }
      break;
    }
  }
}

orc::ParameterValue StageParameterDialog::get_widget_value(
    const std::string& param_name) const {
  auto it = parameter_widgets_.find(param_name);
  if (it == parameter_widgets_.end()) {
    return static_cast<int32_t>(0);  // Should never happen
  }

  const auto& pw = it->second;

  switch (pw.type) {
    case orc::ParameterType::INT32:
      return static_cast<int32_t>(static_cast<QSpinBox*>(pw.widget)->value());
    case orc::ParameterType::UINT32:
      return static_cast<uint32_t>(static_cast<QSpinBox*>(pw.widget)->value());
    case orc::ParameterType::DOUBLE:
      return static_cast<QDoubleSpinBox*>(pw.widget)->value();
    case orc::ParameterType::BOOL:
      return static_cast<QCheckBox*>(pw.widget)->isChecked();
    case orc::ParameterType::STRING:
      if (auto* combo = qobject_cast<QComboBox*>(pw.widget)) {
        const QVariant data = combo->currentData();
        return data.isValid() ? data.toString().toStdString()
                              : combo->currentText().toStdString();
      } else if (auto* edit = qobject_cast<QLineEdit*>(pw.widget)) {
        return from_display_spec(param_name, edit->text().toStdString());
      }
      break;
    case orc::ParameterType::FILE_PATH: {
      // For FILE_PATH, the widget is a container with a QLineEdit inside
      auto* edit = pw.widget->findChild<QLineEdit*>("file_path_edit");
      if (edit) {
        return edit->text().toStdString();
      }
      break;
    }
  }

  return static_cast<int32_t>(0);  // Should never happen
}

std::string StageParameterDialog::to_display_spec(
    const std::string& param_name, const std::string& stored_value) const {
  const auto kind = orc::indexed_spec_kind(stage_name_, param_name);
  return orc::indexed_spec_to_presentation(kind, stored_value);
}

std::string StageParameterDialog::from_display_spec(
    const std::string& param_name, const std::string& display_value) const {
  const auto kind = orc::indexed_spec_kind(stage_name_, param_name);
  auto stored = orc::indexed_spec_from_presentation(kind, display_value);
  return stored ? *stored : display_value;
}

void StageParameterDialog::on_reset_defaults() {
  for (const auto& desc : descriptors_) {
    bool value_applied = false;

    if (reset_values_.has_value()) {
      auto reset_it = reset_values_->find(desc.name);
      if (reset_it != reset_values_->end()) {
        set_widget_value(desc.name, reset_it->second);
        value_applied = true;
      }
    }

    if (!value_applied && desc.constraints.default_value.has_value()) {
      set_widget_value(desc.name, *desc.constraints.default_value);
    }
  }
}

bool StageParameterDialog::validate_values() {
  // Helper: resolve a potentially-relative path to absolute using the project
  // directory
  auto resolve_path = [this](const QString& path) -> QString {
    if (path.isEmpty() || project_path_.isEmpty()) return path;
    if (QFileInfo(path).isAbsolute()) return path;
    return QDir(QFileInfo(project_path_).absolutePath()).filePath(path);
  };

  // Helper: verify the SQLite metadata (.tbc.db) file exists; accept a legacy
  // JSON file with an info log, or warn generically if nothing is found.
  auto check_db_file = [this](const QString& db_path) -> bool {
    if (db_path.isEmpty()) return true;
    if (QFileInfo::exists(db_path)) return true;

    if (db_path.endsWith(".db", Qt::CaseInsensitive)) {
      QString json_path = db_path.left(db_path.length() - 3) + ".json";
      if (QFileInfo::exists(json_path)) {
        ORC_LOG_INFO(
            "TBC source '{}' has legacy JSON metadata; consider re-decoding "
            "with a current version of ld-decode/vhs-decode",
            QFileInfo(json_path).fileName().toStdString());
        return true;
      }
    }
    QMessageBox::warning(this, "Missing Metadata File",
                         QString("Metadata file not found:\n%1\n\nRe-run the "
                                 "decoder to generate a .tbc.db metadata file.")
                             .arg(db_path));
    return false;
  };

  // tbc_source derives db_path from input_path at runtime (input_path + ".db").
  // CVBS source stages also use input_path but do not require a .tbc.db
  // sidecar.
  const bool requires_derived_db_metadata = (stage_name_ == "tbc_source");

  if (requires_derived_db_metadata) {
    auto input_it = parameter_widgets_.find("input_path");
    if (input_it != parameter_widgets_.end()) {
      auto* edit =
          input_it->second.widget->findChild<QLineEdit*>("file_path_edit");
      if (edit && !edit->text().isEmpty()) {
        if (!check_db_file(resolve_path(edit->text()) + ".db")) return false;
      }
    }
  }

  // YC source stages: db_path is an explicit parameter
  auto db_it = parameter_widgets_.find("db_path");
  if (db_it != parameter_widgets_.end()) {
    auto* edit = db_it->second.widget->findChild<QLineEdit*>("file_path_edit");
    if (edit && !edit->text().isEmpty()) {
      if (!check_db_file(resolve_path(edit->text()))) return false;
    }
  }

  // Cross-parameter constraints for video parameter overrides.
  auto get_int_param =
      [this](const std::string& param_name) -> std::optional<int32_t> {
    auto it = parameter_widgets_.find(param_name);
    if (it == parameter_widgets_.end()) {
      return std::nullopt;
    }

    const auto value = get_widget_value(param_name);
    if (const auto* int_value = std::get_if<int32_t>(&value)) {
      return *int_value;
    }
    if (const auto* uint_value = std::get_if<uint32_t>(&value)) {
      return static_cast<int32_t>(*uint_value);
    }

    return std::nullopt;
  };

  auto require_less_than_if_set =
      [](const std::optional<int32_t>& lhs,
         const std::optional<int32_t>& rhs) -> bool {
    if (!lhs.has_value() || !rhs.has_value()) {
      return true;
    }
    // Value -1 means "use source value" for video params and should not fail
    // validation.
    if (lhs.value() < 0 || rhs.value() < 0) {
      return true;
    }
    return lhs.value() < rhs.value();
  };

  QStringList validation_errors;

  // Indexed spec parameters (frame/line ranges) are entered 1-based in the
  // UI; verify they convert cleanly to the stored 0-based form.
  for (const auto& desc : descriptors_) {
    const auto kind = orc::indexed_spec_kind(stage_name_, desc.name);
    if (kind == orc::IndexedSpecKind::kNone) continue;

    auto widget_it = parameter_widgets_.find(desc.name);
    if (widget_it == parameter_widgets_.end()) continue;
    auto* edit = qobject_cast<QLineEdit*>(widget_it->second.widget);
    if (edit == nullptr) continue;

    const std::string display_value = edit->text().toStdString();

    // Unmodified values (including unrecognised legacy specs shown verbatim)
    // pass through to the stage untouched.
    auto baseline_it = spec_display_baseline_.find(desc.name);
    if (baseline_it != spec_display_baseline_.end() &&
        baseline_it->second == display_value) {
      continue;
    }

    if (!orc::indexed_spec_from_presentation(kind, display_value)) {
      const QString example = (kind == orc::IndexedSpecKind::kDropoutMapSpec)
                                  ? "'[{frame:1,add:[{line:22,start:100,"
                                    "end:200}]}]'"
                                  : "'1-11,21-31'";
      validation_errors << QString(
                               "%1: invalid specification. Frame and line "
                               "numbers are 1-based (matching the preview); "
                               "for example %2.")
                               .arg(QString::fromStdString(desc.display_name))
                               .arg(example);
    }
  }

  const auto colour_burst_start = get_int_param("colourBurstStart");
  const auto colour_burst_end = get_int_param("colourBurstEnd");
  const auto active_video_start = get_int_param("activeVideoStart");
  const auto active_video_end = get_int_param("activeVideoEnd");
  const auto first_active_field_line = get_int_param("firstActiveFieldLine");
  const auto last_active_field_line = get_int_param("lastActiveFieldLine");
  const auto black_ire = get_int_param("blackLevel");
  const auto white_ire = get_int_param("whiteLevel");

  if (!require_less_than_if_set(colour_burst_start, colour_burst_end)) {
    validation_errors
        << "Colour Burst Start must be less than Colour Burst End.";
  }

  if (!require_less_than_if_set(active_video_start, active_video_end)) {
    validation_errors
        << "Active Video Start must be less than Active Video End.";
  }

  if (!require_less_than_if_set(colour_burst_start, active_video_start)) {
    validation_errors
        << "Colour Burst Start must be before Active Video Start.";
  }

  if (!require_less_than_if_set(colour_burst_end, active_video_start)) {
    validation_errors << "Colour Burst End must be before Active Video Start.";
  }

  if (!require_less_than_if_set(first_active_field_line,
                                last_active_field_line)) {
    validation_errors
        << "First Active Field Line must be less than Last Active Field Line.";
  }

  if (!require_less_than_if_set(black_ire, white_ire)) {
    validation_errors << "Black IRE must be less than White IRE.";
  }

  if (!validation_errors.isEmpty()) {
    QMessageBox::warning(this, "Invalid Parameters",
                         validation_errors.join("\n"));
    return false;
  }

  return true;
}

void StageParameterDialog::on_validate_and_accept() {
  if (validate_values()) {
    accept();
  }
}

void StageParameterDialog::on_validate_and_update() {
  if (validate_values()) {
    emit update_requested();
  }
}

void StageParameterDialog::update_dependencies() {
  // Get current values of all parameters
  std::map<std::string, orc::ParameterValue> current_values;
  std::map<std::string, const orc::ParameterDescriptor*> descriptors_by_name;
  for (const auto& desc : descriptors_) {
    current_values[desc.name] = get_widget_value(desc.name);
    descriptors_by_name[desc.name] = &desc;
  }

  // A parameter is active only when its own dependency is met AND the
  // parameter it depends on is itself active (so e.g. encoder options keyed
  // on ffmpeg_format deactivate together with ffmpeg_format when
  // output_mode is "raw"). The visited set guards against dependency cycles.
  std::function<bool(const orc::ParameterDescriptor&, std::set<std::string>&)>
      is_active = [&](const orc::ParameterDescriptor& desc,
                      std::set<std::string>& visited) -> bool {
    if (!desc.constraints.depends_on.has_value()) {
      return true;
    }
    if (!visited.insert(desc.name).second) {
      return true;  // Cycle: treat as active rather than hiding everything
    }

    const auto& dep = *desc.constraints.depends_on;
    auto it = current_values.find(dep.parameter_name);
    if (it == current_values.end()) {
      return false;
    }

    std::string current_val = orc::parameter_util::value_to_string(it->second);
    bool satisfied =
        std::find(dep.required_values.begin(), dep.required_values.end(),
                  current_val) != dep.required_values.end();
    if (!satisfied) {
      return false;
    }

    auto parent_it = descriptors_by_name.find(dep.parameter_name);
    if (parent_it == descriptors_by_name.end()) {
      return true;
    }
    return is_active(*parent_it->second, visited);
  };

  // Check each parameter's dependencies
  for (const auto& desc : descriptors_) {
    if (!desc.constraints.depends_on.has_value()) {
      continue;  // No dependency, always enabled
    }

    const auto& dep = *desc.constraints.depends_on;
    std::set<std::string> visited;
    const bool should_enable = is_active(desc, visited);

    // Show/hide or enable/disable the widget and its label row
    auto widget_it = parameter_widgets_.find(desc.name);
    if (widget_it != parameter_widgets_.end()) {
      if (dep.hide_when_disabled) {
        widget_it->second.widget->setVisible(should_enable);
        if (widget_it->second.label) {
          widget_it->second.label->setVisible(should_enable);
        }
      } else {
        widget_it->second.widget->setEnabled(should_enable);
        if (widget_it->second.label) {
          widget_it->second.label->setEnabled(should_enable);
        }
      }
    }
  }
}

std::map<std::string, orc::ParameterValue> StageParameterDialog::get_values()
    const {
  std::map<std::string, orc::ParameterValue> values;

  for (const auto& desc : descriptors_) {
    values[desc.name] = get_widget_value(desc.name);
  }

  return values;
}
