/*
 * File:        stageparameterdialog.cpp
 * Module:      orc-gui
 * Purpose:     Stage parameter dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "stageparameterdialog.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <limits>

StageParameterDialog::StageParameterDialog(
    const std::string& stage_name,
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::map<std::string, orc::ParameterValue>& current_values,
    QWidget* parent)
    : QDialog(parent)
    , descriptors_(descriptors)
{
    setWindowTitle(QString("Edit %1 Parameters").arg(QString::fromStdString(stage_name)));
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
        }
        
        if (widget) {
            // Create label with description as tooltip
            auto* label = new QLabel(QString::fromStdString(desc.display_name) + ":");
            label->setToolTip(QString::fromStdString(desc.description));
            widget->setToolTip(QString::fromStdString(desc.description));
            
            form_layout_->addRow(label, widget);
            parameter_widgets_[desc.name] = ParameterWidget{desc.type, widget};
        }
    }
    
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

std::map<std::string, orc::ParameterValue> StageParameterDialog::get_values() const
{
    std::map<std::string, orc::ParameterValue> values;
    
    for (const auto& desc : descriptors_) {
        values[desc.name] = get_widget_value(desc.name);
    }
    
    return values;
}
