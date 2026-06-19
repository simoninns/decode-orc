/*
 * File:        masklineconfigdialog.cpp
 * Module:      orc-gui
 * Purpose:     Configuration dialog for mask line stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "masklineconfigdialog.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <cmath>
#include <set>
#include <sstream>

static orc::VideoSystem toOrcVideoSystem(orc::presenters::VideoSystem sys) {
  switch (sys) {
    case orc::presenters::VideoSystem::NTSC:
      return orc::VideoSystem::NTSC;
    case orc::presenters::VideoSystem::PAL_M:
      return orc::VideoSystem::PAL_M;
    default:
      return orc::VideoSystem::PAL;
  }
}

MaskLineConfigDialog::MaskLineConfigDialog(QWidget* parent)
    : ConfigDialogBase("Mask Line Configuration", parent), updating_ui_(false) {
  // Create preset configuration group
  auto* preset_group = create_group("Quick Presets");
  auto* preset_layout = qobject_cast<QFormLayout*>(preset_group->layout());

  add_info_label(preset_layout,
                 "<b>Important:</b> All line numbers are <b>0-based field line "
                 "indices</b>, not frame line numbers. "
                 "NTSC/PAL-M: ~262 lines (0-261), PAL: ~312 lines (0-311). "
                 "Traditional 'line 21' = index 20.");

  QStringList presets;
  presets << "None (Custom)"
          << "NTSC Closed Captions"
          << "NTSC VBI Area";
  preset_combo_ = add_combobox(preset_layout, "Preset:", presets,
                               "Select a common line masking preset");

  // Create quick options group
  auto* quick_group = create_group("Quick Options");
  auto* quick_layout = qobject_cast<QFormLayout*>(quick_group->layout());

  ntsc_cc_checkbox_ =
      add_checkbox(quick_layout, "Mask NTSC Closed Captions",
                   "Mask field line 20 of the first field only (NTSC CC data - "
                   "traditional 'line 21' is index 20 in 0-based)");

  ntsc_vbi_checkbox_ = add_checkbox(
      quick_layout, "Mask NTSC VBI Area",
      "Mask field lines 10-20 in both fields (vertical blanking interval)");

  // Create custom configuration group
  auto* custom_group = create_group("Custom Line Range");
  auto* custom_layout = qobject_cast<QFormLayout*>(custom_group->layout());

  custom_enabled_checkbox_ =
      add_checkbox(custom_layout, "Enable Custom Range",
                   "Enable custom line range specification");

  QStringList field_options;
  field_options << "First Field Only" << "Second Field Only" << "Both Fields";
  field_selection_combo_ =
      add_combobox(custom_layout, "Field Selection:", field_options,
                   "Select which field(s) to apply masking to");
  field_selection_combo_->setEnabled(false);

  start_line_spinbox_ =
      add_spinbox(custom_layout, "Start Field Line:", 0, 1000, 0,
                  "First field line number to mask (0-based, NTSC/PAL-M: "
                  "0-261, PAL: 0-311)");
  start_line_spinbox_->setEnabled(false);

  end_line_spinbox_ = add_spinbox(custom_layout, "End Field Line:", 0, 1000, 0,
                                  "Last field line number to mask (0-based, "
                                  "NTSC/PAL-M: 0-261, PAL: 0-311)");
  end_line_spinbox_->setEnabled(false);

  // Create mask level group
  auto* level_group = create_group("Mask Level");
  auto* level_layout = qobject_cast<QFormLayout*>(level_group->layout());

  add_info_label(
      level_layout,
      "Set the 10-bit sample level for masked pixels. "
      "Typical values: 256 = blanking/black, 844 = white (PAL/NTSC).");

  QStringList level_presets;
  level_presets << "Blanking (256)" << "White (844)" << "Custom";
  mask_level_preset_combo_ =
      add_combobox(level_layout, "Level Preset:", level_presets,
                   "Select a preset sample level for masked lines");

  mask_level_spinbox_ =
      add_spinbox(level_layout, "Custom Level:", 0, 1023, 256,
                  "Custom 10-bit sample level for masked pixels (0–1023)");
  mask_level_spinbox_->setEnabled(false);

  // Connect signals
  connect(preset_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MaskLineConfigDialog::on_preset_changed);
  connect(ntsc_cc_checkbox_, &QCheckBox::checkStateChanged, this,
          &MaskLineConfigDialog::on_ntsc_cc_changed);
  connect(custom_enabled_checkbox_, &QCheckBox::checkStateChanged, this,
          &MaskLineConfigDialog::on_custom_enabled_changed);
  connect(mask_level_preset_combo_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MaskLineConfigDialog::on_mask_level_preset_changed);
}

void MaskLineConfigDialog::apply_configuration() {
  std::string line_spec = build_line_spec_from_ui();
  set_parameter("lineSpec", line_spec);

  int32_t blanking = 256, white = 844;
  orc::VideoSystem sys = orc::VideoSystem::PAL;
  resolveVideoLevels(blanking, white, sys);

  int32_t mask_level;
  const int preset_idx = mask_level_preset_combo_->currentIndex();
  if (preset_idx == 1) {  // White
    mask_level = white;
  } else if (preset_idx == 2) {  // Custom — convert display → 10-bit
    const double display_val =
        static_cast<double>(mask_level_spinbox_->value());
    mask_level = orc::display_to_samples10(display_val, blanking, white, sys,
                                           amplitude_unit_);
  } else {  // Blanking (default)
    mask_level = blanking;
  }
  set_parameter("maskSampleLevel", mask_level);
}

void MaskLineConfigDialog::load_from_parameters(
    const std::map<std::string, orc::ParameterValue>& params) {
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

  // Load mask sample level (10-bit)
  auto level_it = params.find("maskSampleLevel");
  if (level_it != params.end() &&
      std::holds_alternative<int32_t>(level_it->second)) {
    int32_t level = std::get<int32_t>(level_it->second);
    int32_t blanking = 256, white = 844;
    orc::VideoSystem sys = orc::VideoSystem::PAL;
    resolveVideoLevels(blanking, white, sys);

    if (level == blanking) {
      mask_level_preset_combo_->setCurrentIndex(0);  // Blanking
    } else if (level == white) {
      mask_level_preset_combo_->setCurrentIndex(1);  // White
    } else {
      mask_level_preset_combo_->setCurrentIndex(2);  // Custom
      const double display_val = orc::samples10_to_display(
          level, blanking, white, sys, amplitude_unit_);
      mask_level_spinbox_->setValue(static_cast<int>(std::round(display_val)));
    }
  } else {
    mask_level_preset_combo_->setCurrentIndex(0);  // Default to blanking
  }

  updating_ui_ = false;
  update_ui_state();
}

void MaskLineConfigDialog::on_preset_changed(int index) {
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

void MaskLineConfigDialog::on_ntsc_cc_changed(Qt::CheckState state) {
  if (!updating_ui_ && state == Qt::Checked) {
    preset_combo_->setCurrentIndex(0);  // Switch to "None (Custom)"
  }
}

void MaskLineConfigDialog::on_custom_enabled_changed(Qt::CheckState state) {
  bool enabled = (state == Qt::Checked);
  field_selection_combo_->setEnabled(enabled);
  start_line_spinbox_->setEnabled(enabled);
  end_line_spinbox_->setEnabled(enabled);

  if (!updating_ui_ && enabled) {
    preset_combo_->setCurrentIndex(0);  // Switch to "None (Custom)"
  }
}

void MaskLineConfigDialog::on_mask_level_preset_changed(int index) {
  bool custom = (index == 2);  // Custom option
  mask_level_spinbox_->setEnabled(custom);

  if (!updating_ui_ && !custom) {
    int32_t blanking = 256, white = 844;
    orc::VideoSystem sys = orc::VideoSystem::PAL;
    resolveVideoLevels(blanking, white, sys);
    const int32_t raw = (index == 1) ? white : blanking;
    const double disp =
        orc::samples10_to_display(raw, blanking, white, sys, amplitude_unit_);
    mask_level_spinbox_->setValue(static_cast<int>(std::round(disp)));
  }
}

void MaskLineConfigDialog::update_ui_state() {
  // Update enable/disable state of custom controls
  bool custom_enabled = custom_enabled_checkbox_->isChecked();
  field_selection_combo_->setEnabled(custom_enabled);
  start_line_spinbox_->setEnabled(custom_enabled);
  end_line_spinbox_->setEnabled(custom_enabled);

  // Update mask level spinbox
  bool level_custom = (mask_level_preset_combo_->currentIndex() == 2);
  mask_level_spinbox_->setEnabled(level_custom);
}

void MaskLineConfigDialog::parse_line_spec_to_ui(const std::string& line_spec) {
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
      (line_spec.find("F:10-20") != std::string::npos &&
       line_spec.find("S:10-20") != std::string::npos)) {
    ntsc_vbi_checkbox_->setChecked(true);
  }

  // Parse custom ranges for the custom section
  // Try to extract a custom range specification
  if (!line_spec.empty()) {
    // Split by commas to get individual specs
    std::vector<std::string> parts;
    std::istringstream iss(line_spec);
    std::string part;
    while (std::getline(iss, part, ',')) {
      parts.push_back(part);
    }

    // Look for a spec that isn't already handled by quick options
    for (const auto& spec : parts) {
      // Skip known quick option specs
      if (spec == "F:20" || spec == "F:10-20" || spec == "S:10-20") {
        continue;
      }

      // Try to parse as custom range: [F|S|A]:[start] or [F|S|A]:[start-end]
      if (spec.size() >= 3 && spec[1] == ':') {
        char parity = spec[0];
        if (parity != 'F' && parity != 'S' && parity != 'A') {
          continue;  // Invalid parity
        }

        std::string range_str = spec.substr(2);
        int start = -1, end = -1;

        // Check for range (start-end) or single value
        size_t dash_pos = range_str.find('-');
        if (dash_pos != std::string::npos) {
          // Range format
          try {
            start = std::stoi(range_str.substr(0, dash_pos));
            end = std::stoi(range_str.substr(dash_pos + 1));
          } catch (...) {
            continue;  // Parse error, skip this spec
          }
        } else {
          // Single value
          try {
            start = end = std::stoi(range_str);
          } catch (...) {
            continue;  // Parse error, skip this spec
          }
        }

        // Valid custom range found - populate UI
        if (start >= 0 && end >= 0) {
          custom_enabled_checkbox_->setChecked(true);

          // Set field selection
          if (parity == 'F') {
            field_selection_combo_->setCurrentIndex(0);  // First field
          } else if (parity == 'S') {
            field_selection_combo_->setCurrentIndex(1);  // Second field
          } else {                                       // 'A'
            field_selection_combo_->setCurrentIndex(2);  // Both fields
          }

          start_line_spinbox_->setValue(start);
          end_line_spinbox_->setValue(end);

          // Only parse the first custom spec found
          break;
        }
      }
    }
  }
}

std::string MaskLineConfigDialog::build_line_spec_from_ui() const {
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

void MaskLineConfigDialog::setAmplitudeUnit(orc::AmplitudeDisplayUnit unit) {
  if (amplitude_unit_ == unit) return;
  amplitude_unit_ = unit;

  // Adjust spinbox range and suffix for the new unit.
  switch (amplitude_unit_) {
    case orc::AmplitudeDisplayUnit::Millivolts:
      mask_level_spinbox_->setRange(-280, 840);
      mask_level_spinbox_->setSuffix(" mV");
      break;
    case orc::AmplitudeDisplayUnit::Samples10Bit:
      mask_level_spinbox_->setRange(0, 1023);
      mask_level_spinbox_->setSuffix("");
      break;
    default:  // IRE
      mask_level_spinbox_->setRange(-40, 120);
      mask_level_spinbox_->setSuffix(" IRE");
      break;
  }

  updatePresetLabels();
}

void MaskLineConfigDialog::setVideoParameters(
    const std::optional<orc::presenters::VideoParametersView>& params) {
  cached_video_params_ = params;
  updatePresetLabels();
}

void MaskLineConfigDialog::updatePresetLabels() {
  int32_t blanking = 256, white = 844;
  orc::VideoSystem sys = orc::VideoSystem::PAL;
  resolveVideoLevels(blanking, white, sys);

  const std::string b_str =
      orc::format_amplitude(blanking, blanking, white, sys, amplitude_unit_);
  const std::string w_str =
      orc::format_amplitude(white, blanking, white, sys, amplitude_unit_);

  mask_level_preset_combo_->setItemText(
      0, QString("Blanking (%1)").arg(QString::fromStdString(b_str)));
  mask_level_preset_combo_->setItemText(
      1, QString("White (%1)").arg(QString::fromStdString(w_str)));
}

void MaskLineConfigDialog::resolveVideoLevels(int32_t& blanking, int32_t& white,
                                              orc::VideoSystem& sys) const {
  blanking = 256;
  white = 844;
  sys = orc::VideoSystem::PAL;
  if (cached_video_params_.has_value()) {
    const auto& vp = *cached_video_params_;
    if (vp.blanking_level >= 0 && vp.white_level > vp.blanking_level) {
      blanking = vp.blanking_level;
      white = vp.white_level;
      sys = toOrcVideoSystem(vp.system);
    }
  }
}
