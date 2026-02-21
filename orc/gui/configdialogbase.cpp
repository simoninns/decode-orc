/*
 * File:        configdialogbase.cpp
 * Module:      orc-gui
 * Purpose:     Base class for simple rule-based configuration dialogs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "configdialogbase.h"
#include <QMessageBox>

ConfigDialogBase::ConfigDialogBase(const QString& title, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setMinimumWidth(500);
    setMinimumHeight(600);

    // Create main layout
    main_layout_ = new QVBoxLayout(this);
    setLayout(main_layout_);

    // Create form layout for controls
    form_layout_ = new QFormLayout();
    main_layout_->addLayout(form_layout_);

    // Add spacer before buttons
    main_layout_->addStretch();

    // Create button box with OK, Cancel, and Reset
    button_box_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset,
        Qt::Horizontal, this);
    main_layout_->addWidget(button_box_);

    // Connect buttons
    connect(button_box_, &QDialogButtonBox::accepted, this, &ConfigDialogBase::on_accept);
    connect(button_box_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(button_box_->button(QDialogButtonBox::Reset), &QPushButton::clicked,
            this, &ConfigDialogBase::on_reset);
}

std::map<std::string, orc::ParameterValue> ConfigDialogBase::get_parameters() const
{
    return parameters_;
}

void ConfigDialogBase::set_parameters(const std::map<std::string, orc::ParameterValue>& params)
{
    parameters_ = params;
    load_from_parameters(params);
}

void ConfigDialogBase::load_from_parameters(const std::map<std::string, orc::ParameterValue>& params)
{
    // Default implementation does nothing
    // Derived classes should override to update UI from parameters
    (void)params;
}

QGroupBox* ConfigDialogBase::create_group(const QString& title)
{
    auto* group = new QGroupBox(title, this);
    auto* layout = new QFormLayout(group);
    group->setLayout(layout);
    main_layout_->insertWidget(main_layout_->count() - 2, group);  // Insert before stretch and buttons
    return group;
}

QCheckBox* ConfigDialogBase::add_checkbox(QFormLayout* layout, const QString& label, 
                                          const QString& tooltip)
{
    auto* checkbox = new QCheckBox(this);
    if (!tooltip.isEmpty()) {
        checkbox->setToolTip(tooltip);
    }
    layout->addRow(label, checkbox);
    return checkbox;
}

QComboBox* ConfigDialogBase::add_combobox(QFormLayout* layout, const QString& label,
                                          const QStringList& items, const QString& tooltip)
{
    auto* combobox = new QComboBox(this);
    combobox->addItems(items);
    if (!tooltip.isEmpty()) {
        combobox->setToolTip(tooltip);
    }
    layout->addRow(label, combobox);
    return combobox;
}

QSpinBox* ConfigDialogBase::add_spinbox(QFormLayout* layout, const QString& label,
                                       int min, int max, int default_value,
                                       const QString& tooltip)
{
    auto* spinbox = new QSpinBox(this);
    spinbox->setMinimum(min);
    spinbox->setMaximum(max);
    spinbox->setValue(default_value);
    if (!tooltip.isEmpty()) {
        spinbox->setToolTip(tooltip);
    }
    layout->addRow(label, spinbox);
    return spinbox;
}

QDoubleSpinBox* ConfigDialogBase::add_double_spinbox(QFormLayout* layout, const QString& label,
                                                     double min, double max, double default_value,
                                                     int decimals, const QString& tooltip)
{
    auto* spinbox = new QDoubleSpinBox(this);
    spinbox->setMinimum(min);
    spinbox->setMaximum(max);
    spinbox->setValue(default_value);
    spinbox->setDecimals(decimals);
    if (!tooltip.isEmpty()) {
        spinbox->setToolTip(tooltip);
    }
    layout->addRow(label, spinbox);
    return spinbox;
}

void ConfigDialogBase::add_info_label(QFormLayout* layout, const QString& text)
{
    auto* label = new QLabel(text, this);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);

    QFont label_font = label->font();
    label_font.setItalic(true);
    label->setFont(label_font);

    QPalette label_palette = label->palette();
    label_palette.setColor(QPalette::WindowText, label_palette.color(QPalette::Disabled, QPalette::WindowText));
    label->setPalette(label_palette);

    layout->addRow(label);
}

void ConfigDialogBase::set_parameter(const std::string& name, const orc::ParameterValue& value)
{
    parameters_[name] = value;
}

orc::ParameterValue ConfigDialogBase::get_parameter(const std::string& name, 
                                                    const orc::ParameterValue& default_value) const
{
    auto it = parameters_.find(name);
    if (it != parameters_.end()) {
        return it->second;
    }
    return default_value;
}

void ConfigDialogBase::on_accept()
{
    // Apply current UI configuration to parameters
    apply_configuration();
    accept();
}

void ConfigDialogBase::on_reset()
{
    reset_to_defaults();
}

void ConfigDialogBase::reset_to_defaults()
{
    // Default implementation clears all parameters
    parameters_.clear();
    load_from_parameters(parameters_);
}
