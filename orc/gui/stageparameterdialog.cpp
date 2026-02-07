/*
 * File:        stageparameterdialog.cpp
 * Module:      orc-gui
 * Purpose:     Stage parameter dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "stageparameterdialog.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <limits>
#include <algorithm>

StageParameterDialog::StageParameterDialog(
    const std::string& stage_name,
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::map<std::string, orc::ParameterValue>& current_values,
    const QString& project_path,
    QWidget* parent)
    : QDialog(parent)
    , stage_name_(stage_name)
    , descriptors_(descriptors)
    , project_path_(project_path)
{
    setWindowTitle(QString("%1 Parameters").arg(QString::fromStdString(stage_name)));
    setMinimumWidth(400);
    
    auto* main_layout = new QVBoxLayout(this);
    
    // Form layout for parameters
    form_layout_ = new QFormLayout();
    main_layout->addLayout(form_layout_);
    
    // Build UI based on descriptors
    build_ui(current_values);
    
    // Reset to defaults button
    reset_button_ = new QPushButton("Reset to Defaults");
    connect(reset_button_, &QPushButton::clicked, this, &StageParameterDialog::on_reset_defaults);
    
    // Dialog buttons
    button_box_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(button_box_, &QDialogButtonBox::accepted, this, &StageParameterDialog::on_validate_and_accept);
    connect(button_box_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    auto* button_layout = new QHBoxLayout();
    button_layout->addWidget(reset_button_);
    button_layout->addStretch();
    button_layout->addWidget(button_box_);
    
    main_layout->addLayout(button_layout);
}

void StageParameterDialog::build_ui(const std::map<std::string, orc::ParameterValue>& current_values)
{
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
                case orc::ParameterType::INT32: value = static_cast<int32_t>(0); break;
                case orc::ParameterType::UINT32: value = static_cast<uint32_t>(0); break;
                case orc::ParameterType::DOUBLE: value = 0.0; break;
                case orc::ParameterType::BOOL: value = false; break;
                case orc::ParameterType::STRING: value = std::string(""); break;
                case orc::ParameterType::FILE_PATH: value = std::string(""); break;
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
                    spin->setMinimum(static_cast<int>(std::get<uint32_t>(*desc.constraints.min_value)));
                } else {
                    spin->setMinimum(0);
                }
                if (desc.constraints.max_value.has_value()) {
                    spin->setMaximum(static_cast<int>(std::get<uint32_t>(*desc.constraints.max_value)));
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
                if (desc.constraints.min_value.has_value()) {
                    spin->setMinimum(std::get<double>(*desc.constraints.min_value));
                } else {
                    spin->setMinimum(-std::numeric_limits<double>::max());
                }
                if (desc.constraints.max_value.has_value()) {
                    spin->setMaximum(std::get<double>(*desc.constraints.max_value));
                } else {
                    spin->setMaximum(std::numeric_limits<double>::max());
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
                    // Use combo box for constrained strings
                    auto* combo = new QComboBox();
                    for (const auto& allowed : desc.constraints.allowed_strings) {
                        combo->addItem(QString::fromStdString(allowed));
                    }
                    combo->setCurrentText(QString::fromStdString(std::get<std::string>(value)));
                    widget = combo;
                } else {
                    // Use line edit for free-form strings
                    auto* edit = new QLineEdit();
                    edit->setText(QString::fromStdString(std::get<std::string>(value)));
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
                
                // Determine if this is an output path (save dialog) or input path (open dialog)
                bool is_output = (stage_name_copy.find("sink") != std::string::npos) ||
                                (param_name.find("output") != std::string::npos) ||
                                (display_name.find("Output") != std::string::npos);
                
                // Connect browse button to file dialog
                connect(browse_btn, &QPushButton::clicked, [this, edit, stage_name_copy, display_name, file_ext_hint, is_output]() {
                    QSettings settings("orc-project", "orc-gui");
                    QString settings_key = QString("lastSourceDirectory/%1").arg(QString::fromStdString(stage_name_copy));
                    
                    // Get last directory for this source type
                    QString last_dir = settings.value(settings_key, QDir::homePath()).toString();
                    
                    // Use current path's directory if it exists, otherwise use last_dir
                    QString start_dir = last_dir;
                    if (!edit->text().isEmpty()) {
                        QFileInfo info(edit->text());
                        if (info.exists() && info.dir().exists()) {
                            start_dir = info.dir().absolutePath();
                        } else if (!edit->text().isEmpty()) {
                            // Path doesn't exist yet (output file) - use its directory if valid
                            QFileInfo parent_info(info.absolutePath());
                            if (parent_info.exists() && parent_info.isDir()) {
                                start_dir = parent_info.absolutePath();
                            }
                        }
                    }
                    
                    // Build file filter based on extension hint
                    QString filter = "All Files (*)";
                    QString dialog_title = is_output ? "Select Output File" : "Select Input File";
                    
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
                        
                        filter = ext_names.mid(1) + " Files (" + ext_patterns + ");;All Files (*)";
                        dialog_title = is_output ? "Select Output " + ext_names.mid(1) + " File" : 
                                                  "Select " + ext_names.mid(1) + " File";
                    }
                    
                    QString file;
                    if (is_output) {
                        file = QFileDialog::getSaveFileName(
                            this,
                            dialog_title,
                            start_dir,
                            filter
                        );
                    } else {
                        file = QFileDialog::getOpenFileName(
                            this,
                            dialog_title,
                            start_dir,
                            filter
                        );
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
                            QLineEdit* pcm_edit = pcm_container->findChild<QLineEdit*>("file_path_edit");
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
                            QLineEdit* efm_edit = efm_container->findChild<QLineEdit*>("file_path_edit");
                            if (efm_edit && efm_edit->text().isEmpty()) {
                                QString efm_path = base_path + ".efm";
                                if (QFileInfo::exists(efm_path)) {
                                    efm_edit->setText(efm_path);
                                }
                            }
                        }
                    });
                }
                
                // Special handling for YC source stages: auto-populate y_path/c_path and pcm_path/efm_path
                if (param_name == "y_path" || param_name == "c_path") {
                    connect(edit, &QLineEdit::textChanged, [this, edit, param_name, stage_name_copy]() {
                        QString current_path = edit->text();
                        if (current_path.isEmpty()) return;
                        
                        // Get base path (remove .tbcy or .tbcc extension if present)
                        QString base_path = current_path;
                        if (base_path.endsWith(".tbcy", Qt::CaseInsensitive)) {
                            base_path = base_path.left(base_path.length() - 5);
                        } else if (base_path.endsWith(".tbcc", Qt::CaseInsensitive)) {
                            base_path = base_path.left(base_path.length() - 5);
                        }
                        
                        // Auto-populate the complementary YC file if it exists
                        if (param_name == "y_path") {
                            // We're setting the Y (luma) file, try to populate C (chroma)
                            auto c_it = parameter_widgets_.find("c_path");
                            if (c_it != parameter_widgets_.end()) {
                                QWidget* c_container = c_it->second.widget;
                                QLineEdit* c_edit = c_container->findChild<QLineEdit*>("file_path_edit");
                                if (c_edit && c_edit->text().isEmpty()) {
                                    QString c_path = base_path + ".tbcc";
                                    if (QFileInfo::exists(c_path)) {
                                        c_edit->setText(c_path);
                                    }
                                }
                            }
                        } else if (param_name == "c_path") {
                            // We're setting the C (chroma) file, try to populate Y (luma)
                            auto y_it = parameter_widgets_.find("y_path");
                            if (y_it != parameter_widgets_.end()) {
                                QWidget* y_container = y_it->second.widget;
                                QLineEdit* y_edit = y_container->findChild<QLineEdit*>("file_path_edit");
                                if (y_edit && y_edit->text().isEmpty()) {
                                    QString y_path = base_path + ".tbcy";
                                    if (QFileInfo::exists(y_path)) {
                                        y_edit->setText(y_path);
                                    }
                                }
                            }
                        }
                        
                        // Auto-populate pcm_path if not already set
                        auto pcm_it = parameter_widgets_.find("pcm_path");
                        if (pcm_it != parameter_widgets_.end()) {
                            QWidget* pcm_container = pcm_it->second.widget;
                            QLineEdit* pcm_edit = pcm_container->findChild<QLineEdit*>("file_path_edit");
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
                            QLineEdit* efm_edit = efm_container->findChild<QLineEdit*>("file_path_edit");
                            if (efm_edit && efm_edit->text().isEmpty()) {
                                QString efm_path = base_path + ".efm";
                                if (QFileInfo::exists(efm_path)) {
                                    efm_edit->setText(efm_path);
                                }
                            }
                        }
                        
                        // Auto-populate db_path if not already set
                        auto db_it = parameter_widgets_.find("db_path");
                        if (db_it != parameter_widgets_.end()) {
                            QWidget* db_container = db_it->second.widget;
                            QLineEdit* db_edit = db_container->findChild<QLineEdit*>("file_path_edit");
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
                        connect(edit, &QLineEdit::textChanged,
                                this, &StageParameterDialog::update_dependencies);
                    }
                    break;
                case orc::ParameterType::INT32:
                case orc::ParameterType::UINT32:
                    connect(static_cast<QSpinBox*>(widget), QOverload<int>::of(&QSpinBox::valueChanged),
                            this, &StageParameterDialog::update_dependencies);
                    break;
                case orc::ParameterType::DOUBLE:
                    connect(static_cast<QDoubleSpinBox*>(widget), QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                            this, &StageParameterDialog::update_dependencies);
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
        form_layout_->addRow(new QLabel("This stage has no configurable parameters."));
        reset_button_->setEnabled(false);
    }
}

void StageParameterDialog::set_widget_value(const std::string& param_name, const orc::ParameterValue& value)
{
    auto it = parameter_widgets_.find(param_name);
    if (it == parameter_widgets_.end()) return;
    
    const auto& pw = it->second;
    
    switch (pw.type) {
        case orc::ParameterType::INT32:
            static_cast<QSpinBox*>(pw.widget)->setValue(std::get<int32_t>(value));
            break;
        case orc::ParameterType::UINT32:
            static_cast<QSpinBox*>(pw.widget)->setValue(static_cast<int>(std::get<uint32_t>(value)));
            break;
        case orc::ParameterType::DOUBLE:
            static_cast<QDoubleSpinBox*>(pw.widget)->setValue(std::get<double>(value));
            break;
        case orc::ParameterType::BOOL:
            static_cast<QCheckBox*>(pw.widget)->setChecked(std::get<bool>(value));
            break;
        case orc::ParameterType::STRING:
            if (auto* combo = qobject_cast<QComboBox*>(pw.widget)) {
                combo->setCurrentText(QString::fromStdString(std::get<std::string>(value)));
            } else if (auto* edit = qobject_cast<QLineEdit*>(pw.widget)) {
                edit->setText(QString::fromStdString(std::get<std::string>(value)));
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

orc::ParameterValue StageParameterDialog::get_widget_value(const std::string& param_name) const
{
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
                return combo->currentText().toStdString();
            } else if (auto* edit = qobject_cast<QLineEdit*>(pw.widget)) {
                return edit->text().toStdString();
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

void StageParameterDialog::on_reset_defaults()
{
    for (const auto& desc : descriptors_) {
        if (desc.constraints.default_value.has_value()) {
            set_widget_value(desc.name, *desc.constraints.default_value);
        }
    }
}

bool StageParameterDialog::validate_values()
{
    // Basic validation - Qt widgets already enforce min/max
    // Could add additional validation here if needed
    return true;
}

void StageParameterDialog::on_validate_and_accept()
{
    if (validate_values()) {
        accept();
    } else {
        QMessageBox::warning(this, "Invalid Parameters", 
                            "One or more parameter values are invalid.");
    }
}

void StageParameterDialog::update_dependencies()
{
    // Get current values of all parameters
    std::map<std::string, orc::ParameterValue> current_values;
    for (const auto& desc : descriptors_) {
        current_values[desc.name] = get_widget_value(desc.name);
    }
    
    // Check each parameter's dependencies
    for (const auto& desc : descriptors_) {
        if (!desc.constraints.depends_on.has_value()) {
            continue;  // No dependency, always enabled
        }
        
        const auto& dep = *desc.constraints.depends_on;
        bool should_enable = false;
        
        // Find the value of the parameter we depend on
        auto it = current_values.find(dep.parameter_name);
        if (it != current_values.end()) {
            // Convert current value to string for comparison
            std::string current_val = orc::parameter_util::value_to_string(it->second);
            
            // Check if current value is in the list of required values
            should_enable = std::find(dep.required_values.begin(), 
                                     dep.required_values.end(), 
                                     current_val) != dep.required_values.end();
        }
        
        // Enable or disable the widget and label
        auto widget_it = parameter_widgets_.find(desc.name);
        if (widget_it != parameter_widgets_.end()) {
            widget_it->second.widget->setEnabled(should_enable);
            if (widget_it->second.label) {
                widget_it->second.label->setEnabled(should_enable);
            }
        }
    }
}

std::map<std::string, orc::ParameterValue> StageParameterDialog::get_values() const
{
    std::map<std::string, orc::ParameterValue> values;
    
    for (const auto& desc : descriptors_) {
        values[desc.name] = get_widget_value(desc.name);
    }
    
    return values;
}
