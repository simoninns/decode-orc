/*
 * File:        masklineconfigdialog.cpp
 * Module:      orc-gui
 * Purpose:     Configuration dialog for mask line stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "masklineconfigdialog.h"
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <sstream>
#include <set>

MaskLineConfigDialog::MaskLineConfigDialog(QWidget *parent)
    : ConfigDialogBase("Mask Line Configuration", parent),
      updating_ui_(false)
{
    // Create preset configuration group
    auto* preset_group = create_group("Quick Presets");
    auto* preset_layout = qobject_cast<QFormLayout*>(preset_group->layout());

    add_info_label(preset_layout, 
        "<b>Important:</b> All line numbers are <b>0-based field line indices</b>, not frame line numbers. "
        "Each field contains ~262 lines (NTSC) or ~312 lines (PAL). Traditional 'line 21' = index 20.");

    QStringList presets;
    presets << "None (Custom)" 
            << "NTSC Closed Captions" 
            << "NTSC VBI Area";
    preset_combo_ = add_combobox(preset_layout, "Preset:", presets,
        "Select a common line masking preset");

    // Create quick options group
    auto* quick_group = create_group("Quick Options");
    auto* quick_layout = qobject_cast<QFormLayout*>(quick_group->layout());

    ntsc_cc_checkbox_ = add_checkbox(quick_layout, "Mask NTSC Closed Captions",
        "Mask field line 20 of the first field only (NTSC CC data - traditional 'line 21' is index 20 in 0-based)");

    ntsc_vbi_checkbox_ = add_checkbox(quick_layout, "Mask NTSC VBI Area",
        "Mask field lines 10-20 in both fields (vertical blanking interval)");

    // Create custom configuration group
    auto* custom_group = create_group("Custom Line Range");
    auto* custom_layout = qobject_cast<QFormLayout*>(custom_group->layout());

    custom_enabled_checkbox_ = add_checkbox(custom_layout, "Enable Custom Range",
        "Enable custom line range specification");

    QStringList field_options;
    field_options << "First Field Only" << "Second Field Only" << "Both Fields";
    field_selection_combo_ = add_combobox(custom_layout, "Field Selection:", field_options,
        "Select which field(s) to apply masking to");
    field_selection_combo_->setEnabled(false);

    start_line_spinbox_ = add_spinbox(custom_layout, "Start Field Line:", 0, 1000, 0,
        "First field line number to mask (0-based, range 0-261 for NTSC, 0-311 for PAL)");
    start_line_spinbox_->setEnabled(false);

    end_line_spinbox_ = add_spinbox(custom_layout, "End Field Line:", 0, 1000, 0,
        "Last field line number to mask (0-based, range 0-261 for NTSC, 0-311 for PAL)");
    end_line_spinbox_->setEnabled(false);

    // Create mask level group
    auto* level_group = create_group("Mask Level");
    auto* level_layout = qobject_cast<QFormLayout*>(level_group->layout());

    add_info_label(level_layout,
        "Set the IRE level for masked pixels (0 = black, 100 = white).");

    QStringList level_presets;
    level_presets << "Black (0 IRE)" << "Gray (50 IRE)" << "White (100 IRE)" << "Custom";
    mask_level_preset_combo_ = add_combobox(level_layout, "Level Preset:", level_presets,
        "Select a preset IRE level for masked lines");

    mask_ire_spinbox_ = add_double_spinbox(level_layout, "Custom IRE:", 0.0, 100.0, 0.0, 1,
        "Custom IRE level for masked pixels");
    mask_ire_spinbox_->setEnabled(false);

    // Connect signals
    connect(preset_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MaskLineConfigDialog::on_preset_changed);
    connect(ntsc_cc_checkbox_, &QCheckBox::checkStateChanged,
            this, &MaskLineConfigDialog::on_ntsc_cc_changed);
    connect(custom_enabled_checkbox_, &QCheckBox::checkStateChanged,
            this, &MaskLineConfigDialog::on_custom_enabled_changed);
    connect(mask_level_preset_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MaskLineConfigDialog::on_mask_level_preset_changed);
}

void MaskLineConfigDialog::apply_configuration()
{
    // Build line specification from UI state
    std::string line_spec = build_line_spec_from_ui();
    set_parameter("lineSpec", line_spec);

    // Get mask IRE level
    double mask_ire;
    if (mask_level_preset_combo_->currentIndex() == 3) {  // Custom
        mask_ire = mask_ire_spinbox_->value();
    } else if (mask_level_preset_combo_->currentIndex() == 1) {  // Gray
        mask_ire = 50.0;
    } else if (mask_level_preset_combo_->currentIndex() == 2) {  // White
        mask_ire = 100.0;
    } else {  // Black
        mask_ire = 0.0;
    }
    set_parameter("maskIRE", mask_ire);
}

void MaskLineConfigDialog::load_from_parameters(const std::map<std::string, orc::ParameterValue>& params)
{
    updating_ui_ = true;

    // Load line spec
    auto it = params.find("lineSpec");
    if (it != params.end() && std::holds_alternative<std::string>(it->second)) {
        const std::string& line_spec = std::get<std::string>(it->second);
        parse_line_spec_to_ui(line_spec);
    } else {
        // No line spec - reset to defaults
        preset_combo_->setCurrentIndex(0);
        ntsc_cc_checkbox_->setChecked(false);
        ntsc_vbi_checkbox_->setChecked(false);
        custom_enabled_checkbox_->setChecked(false);
    }

    // Load mask IRE level
    auto ire_it = params.find("maskIRE");
    if (ire_it != params.end() && std::holds_alternative<double>(ire_it->second)) {
        double ire = std::get<double>(ire_it->second);
        
        // Set preset combo based on IRE value
        if (ire == 0.0) {
            mask_level_preset_combo_->setCurrentIndex(0);  // Black
        } else if (ire == 50.0) {
            mask_level_preset_combo_->setCurrentIndex(1);  // Gray
        } else if (ire == 100.0) {
            mask_level_preset_combo_->setCurrentIndex(2);  // White
        } else {
            mask_level_preset_combo_->setCurrentIndex(3);  // Custom
            mask_ire_spinbox_->setValue(ire);
        }
    } else {
        mask_level_preset_combo_->setCurrentIndex(0);  // Default to black
    }

    updating_ui_ = false;
    update_ui_state();
}

void MaskLineConfigDialog::on_preset_changed(int index)
{
    if (updating_ui_) return;

    updating_ui_ = true;

    // Clear all quick options
    ntsc_cc_checkbox_->setChecked(false);
    ntsc_vbi_checkbox_->setChecked(false);
    custom_enabled_checkbox_->setChecked(false);

    // Set based on preset
    switch (index) {
        case 1:  // NTSC Closed Captions
            ntsc_cc_checkbox_->setChecked(true);
            break;
        case 2:  // NTSC VBI Area
            ntsc_vbi_checkbox_->setChecked(true);
            break;
        default:  // None (Custom)
            break;
    }

    updating_ui_ = false;
}

void MaskLineConfigDialog::on_ntsc_cc_changed(Qt::CheckState state)
{
    if (!updating_ui_ && state == Qt::Checked) {
        preset_combo_->setCurrentIndex(0);  // Switch to "None (Custom)"
    }
}

void MaskLineConfigDialog::on_custom_enabled_changed(Qt::CheckState state)
{
    bool enabled = (state == Qt::Checked);
    field_selection_combo_->setEnabled(enabled);
    start_line_spinbox_->setEnabled(enabled);
    end_line_spinbox_->setEnabled(enabled);

    if (!updating_ui_ && enabled) {
        preset_combo_->setCurrentIndex(0);  // Switch to "None (Custom)"
    }
}

void MaskLineConfigDialog::on_mask_level_preset_changed(int index)
{
    bool custom = (index == 3);  // Custom option
    mask_ire_spinbox_->setEnabled(custom);

    if (!updating_ui_ && !custom) {
        // Update spinbox to show the preset value
        if (index == 0) {  // Black
            mask_ire_spinbox_->setValue(0.0);
        } else if (index == 1) {  // Gray
            mask_ire_spinbox_->setValue(50.0);
        } else if (index == 2) {  // White
            mask_ire_spinbox_->setValue(100.0);
        }
    }
}

void MaskLineConfigDialog::update_ui_state()
{
    // Update enable/disable state of custom controls
    bool custom_enabled = custom_enabled_checkbox_->isChecked();
    field_selection_combo_->setEnabled(custom_enabled);
    start_line_spinbox_->setEnabled(custom_enabled);
    end_line_spinbox_->setEnabled(custom_enabled);

    // Update mask IRE spinbox
    bool ire_custom = (mask_level_preset_combo_->currentIndex() == 3);
    mask_ire_spinbox_->setEnabled(ire_custom);
}

void MaskLineConfigDialog::parse_line_spec_to_ui(const std::string& line_spec)
{
    // Simple parser to detect common patterns and set UI accordingly
    
    // Check for common presets
    if (line_spec == "F:20") {
        preset_combo_->setCurrentIndex(1);  // NTSC CC
        ntsc_cc_checkbox_->setChecked(true);
        return;
    } else if (line_spec == "F:10-20,S:10-20") {
        preset_combo_->setCurrentIndex(2);  // NTSC VBI
        ntsc_vbi_checkbox_->setChecked(true);
        return;
    }

    // If not a simple preset, check for combinations
    preset_combo_->setCurrentIndex(0);  // None (Custom)
    
    // Check each pattern
    if (line_spec.find("F:20") != std::string::npos) {
        ntsc_cc_checkbox_->setChecked(true);
    }
    if (line_spec.find("F:10-20,S:10-20") != std::string::npos || 
        (line_spec.find("F:10-20") != std::string::npos && line_spec.find("S:10-20") != std::string::npos)) {
        ntsc_vbi_checkbox_->setChecked(true);
    }

    // TODO: Parse custom ranges for the custom section
    // For now, custom section is not auto-populated from complex specs
}

std::string MaskLineConfigDialog::build_line_spec_from_ui() const
{
    std::vector<std::string> specs;

    // Add quick options
    if (ntsc_cc_checkbox_->isChecked()) {
        specs.push_back("F:20");
    }
    if (ntsc_vbi_checkbox_->isChecked()) {
        specs.push_back("F:10-20,S:10-20");
    }

    // Add custom range if enabled
    if (custom_enabled_checkbox_->isChecked()) {
        char parity;
        int field_idx = field_selection_combo_->currentIndex();
        if (field_idx == 0) {
            parity = 'F';  // First field
        } else if (field_idx == 1) {
            parity = 'S';  // Second field
        } else {
            parity = 'A';  // All fields
        }

        int start = start_line_spinbox_->value();
        int end = end_line_spinbox_->value();

        std::ostringstream custom_spec;
        custom_spec << parity << ":";
        if (start == end) {
            custom_spec << start;
        } else {
            custom_spec << start << "-" << end;
        }
        specs.push_back(custom_spec.str());
    }

    // Combine all specs with commas
    std::string result;
    for (size_t i = 0; i < specs.size(); ++i) {
        if (i > 0) result += ",";
        result += specs[i];
    }

    return result;
}
