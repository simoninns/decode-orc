/*
 * File:        config_dialog_base_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Rule-application widget tests for ConfigDialogBase subclasses
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <map>
#include <string>

#include "ffmpegpresetdialog.h"
#include "masklineconfigdialog.h"

namespace gui_unit_test {
namespace {

QApplication& ensureApplication() {
  if (auto* existing_app =
          qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *existing_app;
  }

  static int argc = 3;
  static char app_name[] = "orc-gui-config-dialog-base-test";
  static char platform_opt[] = "-platform";
  static char platform_val[] = "offscreen";
  static char* argv[] = {app_name, platform_opt, platform_val, nullptr};
  static QApplication* app = [] {
    auto* created_app = new QApplication(argc, argv);
    created_app->setQuitOnLastWindowClosed(false);
    return created_app;
  }();

  return *app;
}

QGroupBox* findGroupByTitle(QWidget& parent, const QString& title) {
  const auto groups = parent.findChildren<QGroupBox*>();
  for (auto* group : groups) {
    if (group != nullptr && group->title() == title) {
      return group;
    }
  }
  return nullptr;
}

QWidget* fieldWidgetByRowLabel(QGroupBox& group, const QString& row_label) {
  auto* layout = qobject_cast<QFormLayout*>(group.layout());
  if (layout == nullptr) {
    return nullptr;
  }

  for (int row = 0; row < layout->rowCount(); ++row) {
    auto* label_item = layout->itemAt(row, QFormLayout::LabelRole);
    auto* field_item = layout->itemAt(row, QFormLayout::FieldRole);
    if (label_item == nullptr || field_item == nullptr) {
      continue;
    }

    auto* label = qobject_cast<QLabel*>(label_item->widget());
    if (label != nullptr && label->text() == row_label) {
      return field_item->widget();
    }
  }

  return nullptr;
}

void clickOk(ConfigDialogBase& dialog) {
  auto* button_box = dialog.findChild<QDialogButtonBox*>();
  ASSERT_NE(button_box, nullptr);

  auto* ok_button = button_box->button(QDialogButtonBox::Ok);
  ASSERT_NE(ok_button, nullptr);

  ok_button->click();
  QCoreApplication::processEvents();
}

}  // namespace

TEST(ConfigDialogBaseTest, maskLineDialogAppliesPresetAndMaskLevelRules) {
  (void)ensureApplication();

  MaskLineConfigDialog dialog;

  auto* preset_group = findGroupByTitle(dialog, "Quick Presets");
  auto* level_group = findGroupByTitle(dialog, "Mask Level");
  ASSERT_NE(preset_group, nullptr);
  ASSERT_NE(level_group, nullptr);

  auto* preset_combo =
      qobject_cast<QComboBox*>(fieldWidgetByRowLabel(*preset_group, "Preset:"));
  auto* level_preset_combo = qobject_cast<QComboBox*>(
      fieldWidgetByRowLabel(*level_group, "Level Preset:"));
  ASSERT_NE(preset_combo, nullptr);
  ASSERT_NE(level_preset_combo, nullptr);

  preset_combo->setCurrentIndex(1);        // NTSC Closed Captions
  level_preset_combo->setCurrentIndex(2);  // White (100 IRE)

  clickOk(dialog);

  const auto params = dialog.get_parameters();
  ASSERT_TRUE(params.find("lineSpec") != params.end());
  ASSERT_TRUE(params.find("maskIRE") != params.end());
  ASSERT_TRUE(std::holds_alternative<std::string>(params.at("lineSpec")));
  ASSERT_TRUE(std::holds_alternative<double>(params.at("maskIRE")));

  EXPECT_EQ(std::get<std::string>(params.at("lineSpec")), "F:20");
  EXPECT_DOUBLE_EQ(std::get<double>(params.at("maskIRE")), 100.0);
}

TEST(ConfigDialogBaseTest, maskLineDialogAppliesCustomRangeRule) {
  (void)ensureApplication();

  MaskLineConfigDialog dialog;

  auto* custom_group = findGroupByTitle(dialog, "Custom Line Range");
  auto* level_group = findGroupByTitle(dialog, "Mask Level");
  ASSERT_NE(custom_group, nullptr);
  ASSERT_NE(level_group, nullptr);

  auto* custom_enabled = qobject_cast<QCheckBox*>(
      fieldWidgetByRowLabel(*custom_group, "Enable Custom Range"));
  auto* field_selection = qobject_cast<QComboBox*>(
      fieldWidgetByRowLabel(*custom_group, "Field Selection:"));
  auto* start_line = qobject_cast<QSpinBox*>(
      fieldWidgetByRowLabel(*custom_group, "Start Field Line:"));
  auto* end_line = qobject_cast<QSpinBox*>(
      fieldWidgetByRowLabel(*custom_group, "End Field Line:"));
  auto* level_preset_combo = qobject_cast<QComboBox*>(
      fieldWidgetByRowLabel(*level_group, "Level Preset:"));
  auto* custom_ire = qobject_cast<QDoubleSpinBox*>(
      fieldWidgetByRowLabel(*level_group, "Custom IRE:"));
  ASSERT_NE(custom_enabled, nullptr);
  ASSERT_NE(field_selection, nullptr);
  ASSERT_NE(start_line, nullptr);
  ASSERT_NE(end_line, nullptr);
  ASSERT_NE(level_preset_combo, nullptr);
  ASSERT_NE(custom_ire, nullptr);

  EXPECT_FALSE(field_selection->isEnabled());
  EXPECT_FALSE(start_line->isEnabled());
  EXPECT_FALSE(end_line->isEnabled());

  custom_enabled->setChecked(true);
  EXPECT_TRUE(field_selection->isEnabled());
  EXPECT_TRUE(start_line->isEnabled());
  EXPECT_TRUE(end_line->isEnabled());

  field_selection->setCurrentIndex(1);  // Second field only
  start_line->setValue(4);
  end_line->setValue(7);

  level_preset_combo->setCurrentIndex(3);  // Custom
  EXPECT_TRUE(custom_ire->isEnabled());
  custom_ire->setValue(12.5);

  clickOk(dialog);

  const auto params = dialog.get_parameters();
  ASSERT_TRUE(params.find("lineSpec") != params.end());
  ASSERT_TRUE(params.find("maskIRE") != params.end());
  ASSERT_TRUE(std::holds_alternative<std::string>(params.at("lineSpec")));
  ASSERT_TRUE(std::holds_alternative<double>(params.at("maskIRE")));

  EXPECT_EQ(std::get<std::string>(params.at("lineSpec")), "S:4-7");
  EXPECT_DOUBLE_EQ(std::get<double>(params.at("maskIRE")), 12.5);
}

TEST(ConfigDialogBaseTest,
     ffmpegDialogUpdatesFilenameExtensionWhenCategoryChanges) {
  (void)ensureApplication();

  FFmpegPresetDialog dialog;

  auto* category_group = findGroupByTitle(dialog, "Export Category");
  auto* filename_group = findGroupByTitle(dialog, "Output Filename");
  ASSERT_NE(category_group, nullptr);
  ASSERT_NE(filename_group, nullptr);

  auto* category_combo = qobject_cast<QComboBox*>(
      fieldWidgetByRowLabel(*category_group, "Category:"));
  auto* filename_container =
      fieldWidgetByRowLabel(*filename_group, "Filename:");
  ASSERT_NE(category_combo, nullptr);
  ASSERT_NE(filename_container, nullptr);

  auto* filename_edit = filename_container->findChild<QLineEdit*>();
  ASSERT_NE(filename_edit, nullptr);

  filename_edit->setText("capture.mp4");
  category_combo->setCurrentIndex(3);  // Broadcast (MXF)
  QCoreApplication::processEvents();

  EXPECT_TRUE(filename_edit->text().endsWith(".mxf"));
}

TEST(ConfigDialogBaseTest, ffmpegDialogAppliesPresetAndOptionRules) {
  (void)ensureApplication();

  FFmpegPresetDialog dialog;

  auto* category_group = findGroupByTitle(dialog, "Export Category");
  auto* preset_group = findGroupByTitle(dialog, "Preset Selection");
  auto* filename_group = findGroupByTitle(dialog, "Output Filename");
  auto* options_group = findGroupByTitle(dialog, "Export Options");
  auto* advanced_group =
      findGroupByTitle(dialog, "Advanced Settings (Optional)");
  ASSERT_NE(category_group, nullptr);
  ASSERT_NE(preset_group, nullptr);
  ASSERT_NE(filename_group, nullptr);
  ASSERT_NE(options_group, nullptr);
  ASSERT_NE(advanced_group, nullptr);

  auto* category_combo = qobject_cast<QComboBox*>(
      fieldWidgetByRowLabel(*category_group, "Category:"));
  auto* preset_combo =
      qobject_cast<QComboBox*>(fieldWidgetByRowLabel(*preset_group, "Preset:"));
  auto* filename_container =
      fieldWidgetByRowLabel(*filename_group, "Filename:");
  auto* deinterlace = qobject_cast<QCheckBox*>(
      fieldWidgetByRowLabel(*options_group, "Deinterlace for web"));
  auto* embed_audio = qobject_cast<QCheckBox*>(
      fieldWidgetByRowLabel(*options_group, "Embed audio"));
  auto* embed_captions = qobject_cast<QCheckBox*>(
      fieldWidgetByRowLabel(*options_group, "Embed closed captions"));
  auto* quality = qobject_cast<QComboBox*>(
      fieldWidgetByRowLabel(*advanced_group, "Encoder Speed:"));
  auto* crf = qobject_cast<QSpinBox*>(
      fieldWidgetByRowLabel(*advanced_group, "Quality (CRF):"));
  auto* bitrate = qobject_cast<QSpinBox*>(
      fieldWidgetByRowLabel(*advanced_group, "Bitrate (Mbps):"));
  ASSERT_NE(category_combo, nullptr);
  ASSERT_NE(preset_combo, nullptr);
  ASSERT_NE(filename_container, nullptr);
  ASSERT_NE(deinterlace, nullptr);
  ASSERT_NE(embed_audio, nullptr);
  ASSERT_NE(embed_captions, nullptr);
  ASSERT_NE(quality, nullptr);
  ASSERT_NE(crf, nullptr);
  ASSERT_NE(bitrate, nullptr);

  auto* filename_edit = filename_container->findChild<QLineEdit*>();
  ASSERT_NE(filename_edit, nullptr);

  category_combo->setCurrentIndex(4);  // Universal (H.264)
  preset_combo->setCurrentIndex(0);    // H.264 (High Quality)
  QCoreApplication::processEvents();

  EXPECT_TRUE(deinterlace->isEnabled());

  deinterlace->setChecked(true);
  embed_audio->setChecked(true);
  embed_captions->setChecked(true);
  quality->setCurrentIndex(4);  // Very Slow
  crf->setValue(21);
  bitrate->setValue(8);
  filename_edit->setText("exports/out.mp4");

  clickOk(dialog);

  const auto params = dialog.get_parameters();

  ASSERT_TRUE(params.find("output_format") != params.end());
  ASSERT_TRUE(params.find("hardware_encoder") != params.end());
  ASSERT_TRUE(params.find("apply_deinterlace") != params.end());
  ASSERT_TRUE(params.find("encoder_preset") != params.end());
  ASSERT_TRUE(params.find("encoder_crf") != params.end());
  ASSERT_TRUE(params.find("encoder_bitrate") != params.end());
  ASSERT_TRUE(params.find("embed_audio") != params.end());
  ASSERT_TRUE(params.find("embed_closed_captions") != params.end());
  ASSERT_TRUE(params.find("output_path") != params.end());

  ASSERT_TRUE(std::holds_alternative<std::string>(params.at("output_format")));
  ASSERT_TRUE(
      std::holds_alternative<std::string>(params.at("hardware_encoder")));
  ASSERT_TRUE(std::holds_alternative<bool>(params.at("apply_deinterlace")));
  ASSERT_TRUE(std::holds_alternative<std::string>(params.at("encoder_preset")));
  ASSERT_TRUE(std::holds_alternative<int32_t>(params.at("encoder_crf")));
  ASSERT_TRUE(std::holds_alternative<int32_t>(params.at("encoder_bitrate")));
  ASSERT_TRUE(std::holds_alternative<bool>(params.at("embed_audio")));
  ASSERT_TRUE(std::holds_alternative<bool>(params.at("embed_closed_captions")));
  ASSERT_TRUE(std::holds_alternative<std::string>(params.at("output_path")));

  EXPECT_EQ(std::get<std::string>(params.at("output_format")), "mp4-h264");
  EXPECT_EQ(std::get<std::string>(params.at("hardware_encoder")), "none");
  EXPECT_TRUE(std::get<bool>(params.at("apply_deinterlace")));
  EXPECT_EQ(std::get<std::string>(params.at("encoder_preset")), "veryslow");
  EXPECT_EQ(std::get<int32_t>(params.at("encoder_crf")), 21);
  EXPECT_EQ(std::get<int32_t>(params.at("encoder_bitrate")), 8000000);
  EXPECT_TRUE(std::get<bool>(params.at("embed_audio")));
  EXPECT_TRUE(std::get<bool>(params.at("embed_closed_captions")));
  EXPECT_EQ(std::get<std::string>(params.at("output_path")), "exports/out.mp4");
}

}  // namespace gui_unit_test