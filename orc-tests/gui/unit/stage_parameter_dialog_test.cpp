/*
 * File:        stage_parameter_dialog_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Widget tests for StageParameterDialog parameter editing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <map>
#include <string>
#include <vector>

#include "stageparameterdialog.h"

namespace gui_unit_test {
namespace {

QApplication& ensureApplication() {
  if (auto* existing_app =
          qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *existing_app;
  }

  static int argc = 3;
  static char app_name[] = "orc-gui-stage-parameter-dialog-test";
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

QWidget* widgetForDisplayName(StageParameterDialog& dialog,
                              const QString& display_name) {
  auto* form = dialog.findChild<QFormLayout*>();
  if (form == nullptr) {
    return nullptr;
  }

  const QString expected_label = display_name + ":";

  for (int row = 0; row < form->rowCount(); ++row) {
    auto* label_item = form->itemAt(row, QFormLayout::LabelRole);
    auto* field_item = form->itemAt(row, QFormLayout::FieldRole);
    if (label_item == nullptr || field_item == nullptr) {
      continue;
    }

    auto* label = qobject_cast<QLabel*>(label_item->widget());
    if (label != nullptr && label->text() == expected_label) {
      return field_item->widget();
    }
  }

  return nullptr;
}

orc::ParameterDescriptor makeDescriptor(
    const std::string& name, const std::string& display_name,
    const orc::ParameterType type, const orc::ParameterValue& default_value,
    const std::optional<orc::ParameterValue>& min_value = std::nullopt,
    const std::optional<orc::ParameterValue>& max_value = std::nullopt,
    const std::vector<std::string>& allowed_strings = {}) {
  orc::ParameterDescriptor desc;
  desc.name = name;
  desc.display_name = display_name;
  desc.description = display_name + " description";
  desc.type = type;
  desc.constraints.default_value = default_value;
  desc.constraints.min_value = min_value;
  desc.constraints.max_value = max_value;
  desc.constraints.allowed_strings = allowed_strings;
  return desc;
}

}  // namespace

TEST(StageParameterDialogTest,
     Get_ValuesRoundTripsAllSupportedParameterEditorTypes) {
  (void)ensureApplication();

  std::vector<orc::ParameterDescriptor> descriptors;
  descriptors.push_back(
      makeDescriptor("int_param", "Int Param", orc::ParameterType::INT32,
                     static_cast<int32_t>(0), static_cast<int32_t>(-50),
                     static_cast<int32_t>(50)));
  descriptors.push_back(
      makeDescriptor("uint_param", "UInt Param", orc::ParameterType::UINT32,
                     static_cast<uint32_t>(0), static_cast<uint32_t>(0),
                     static_cast<uint32_t>(100)));
  descriptors.push_back(makeDescriptor("double_param", "Double Param",
                                       orc::ParameterType::DOUBLE, 0.0, -10.0,
                                       10.0));
  descriptors.push_back(makeDescriptor("bool_param", "Bool Param",
                                       orc::ParameterType::BOOL, false));
  descriptors.push_back(makeDescriptor("string_param", "String Param",
                                       orc::ParameterType::STRING,
                                       std::string("initial")));
  descriptors.push_back(
      makeDescriptor("enum_param", "Enum Param", orc::ParameterType::STRING,
                     std::string("alpha"), std::nullopt, std::nullopt,
                     {"alpha", "beta", "gamma"}));

  std::map<std::string, orc::ParameterValue> current_values;
  current_values["int_param"] = static_cast<int32_t>(-12);
  current_values["uint_param"] = static_cast<uint32_t>(88);
  current_values["double_param"] = 3.25;
  current_values["bool_param"] = true;
  current_values["string_param"] = std::string("current");
  current_values["enum_param"] = std::string("beta");

  StageParameterDialog dialog("test-stage", "Test Stage",
                              "test stage description", descriptors,
                              current_values);

  auto* int_spin =
      qobject_cast<QSpinBox*>(widgetForDisplayName(dialog, "Int Param"));
  auto* uint_spin =
      qobject_cast<QSpinBox*>(widgetForDisplayName(dialog, "UInt Param"));
  auto* double_spin = qobject_cast<QDoubleSpinBox*>(
      widgetForDisplayName(dialog, "Double Param"));
  auto* bool_check =
      qobject_cast<QCheckBox*>(widgetForDisplayName(dialog, "Bool Param"));
  auto* string_edit =
      qobject_cast<QLineEdit*>(widgetForDisplayName(dialog, "String Param"));
  auto* enum_combo =
      qobject_cast<QComboBox*>(widgetForDisplayName(dialog, "Enum Param"));

  ASSERT_NE(int_spin, nullptr);
  ASSERT_NE(uint_spin, nullptr);
  ASSERT_NE(double_spin, nullptr);
  ASSERT_NE(bool_check, nullptr);
  ASSERT_NE(string_edit, nullptr);
  ASSERT_NE(enum_combo, nullptr);

  int_spin->setValue(31);
  uint_spin->setValue(77);
  double_spin->setValue(-2.5);
  bool_check->setChecked(false);
  string_edit->setText("updated-value");
  enum_combo->setCurrentText("gamma");

  const auto values = dialog.get_values();

  ASSERT_TRUE(values.find("int_param") != values.end());
  ASSERT_TRUE(values.find("uint_param") != values.end());
  ASSERT_TRUE(values.find("double_param") != values.end());
  ASSERT_TRUE(values.find("bool_param") != values.end());
  ASSERT_TRUE(values.find("string_param") != values.end());
  ASSERT_TRUE(values.find("enum_param") != values.end());

  ASSERT_TRUE(std::holds_alternative<int32_t>(values.at("int_param")));
  ASSERT_TRUE(std::holds_alternative<uint32_t>(values.at("uint_param")));
  ASSERT_TRUE(std::holds_alternative<double>(values.at("double_param")));
  ASSERT_TRUE(std::holds_alternative<bool>(values.at("bool_param")));
  ASSERT_TRUE(std::holds_alternative<std::string>(values.at("string_param")));
  ASSERT_TRUE(std::holds_alternative<std::string>(values.at("enum_param")));

  EXPECT_EQ(std::get<int32_t>(values.at("int_param")), 31);
  EXPECT_EQ(std::get<uint32_t>(values.at("uint_param")), 77U);
  EXPECT_DOUBLE_EQ(std::get<double>(values.at("double_param")), -2.5);
  EXPECT_FALSE(std::get<bool>(values.at("bool_param")));
  EXPECT_EQ(std::get<std::string>(values.at("string_param")), "updated-value");
  EXPECT_EQ(std::get<std::string>(values.at("enum_param")), "gamma");
}

TEST(StageParameterDialogTest, Combo_ShowsLabelWhileStoringBareValue) {
  (void)ensureApplication();

  const char sep = StageParameterDialog::kComboValueLabelSeparator;
  std::vector<orc::ParameterDescriptor> descriptors;
  descriptors.push_back(
      makeDescriptor("channel_pair", "Channel pair", orc::ParameterType::STRING,
                     std::string("0"), std::nullopt, std::nullopt,
                     {std::string("0") + sep + "0 - Analogue Audio",
                      std::string("1") + sep + "1 - EFM digital audio"}));

  std::map<std::string, orc::ParameterValue> current_values;
  current_values["channel_pair"] = std::string("1");

  StageParameterDialog dialog("audio_channel_map", "Audio Channel Map", "desc",
                              descriptors, current_values);

  auto* combo =
      qobject_cast<QComboBox*>(widgetForDisplayName(dialog, "Channel pair"));
  ASSERT_NE(combo, nullptr);

  // Display carries the description; the stored value stays the bare index.
  EXPECT_EQ(combo->itemText(0).toStdString(), "0 - Analogue Audio");
  EXPECT_EQ(combo->itemText(1).toStdString(), "1 - EFM digital audio");
  EXPECT_EQ(combo->currentText().toStdString(), "1 - EFM digital audio");

  auto values = dialog.get_values();
  ASSERT_TRUE(std::holds_alternative<std::string>(values.at("channel_pair")));
  EXPECT_EQ(std::get<std::string>(values.at("channel_pair")), "1");

  // Selecting the first entry returns its bare value, not the label.
  combo->setCurrentIndex(0);
  values = dialog.get_values();
  EXPECT_EQ(std::get<std::string>(values.at("channel_pair")), "0");
}

TEST(StageParameterDialogTest,
     Numeric_EditorsRespectConfiguredBoundsAndClampStepping) {
  (void)ensureApplication();

  std::vector<orc::ParameterDescriptor> descriptors;
  descriptors.push_back(
      makeDescriptor("int_param", "Int Param", orc::ParameterType::INT32,
                     static_cast<int32_t>(0), static_cast<int32_t>(-10),
                     static_cast<int32_t>(10)));
  descriptors.push_back(
      makeDescriptor("uint_param", "UInt Param", orc::ParameterType::UINT32,
                     static_cast<uint32_t>(0), static_cast<uint32_t>(1),
                     static_cast<uint32_t>(3)));
  descriptors.push_back(makeDescriptor("double_param", "Double Param",
                                       orc::ParameterType::DOUBLE, 0.0, -0.5,
                                       0.5));

  StageParameterDialog dialog("test-stage", "Test Stage",
                              "test stage description", descriptors, {});

  auto* int_spin =
      qobject_cast<QSpinBox*>(widgetForDisplayName(dialog, "Int Param"));
  auto* uint_spin =
      qobject_cast<QSpinBox*>(widgetForDisplayName(dialog, "UInt Param"));
  auto* double_spin = qobject_cast<QDoubleSpinBox*>(
      widgetForDisplayName(dialog, "Double Param"));

  ASSERT_NE(int_spin, nullptr);
  ASSERT_NE(uint_spin, nullptr);
  ASSERT_NE(double_spin, nullptr);

  EXPECT_EQ(int_spin->minimum(), -10);
  EXPECT_EQ(int_spin->maximum(), 10);
  EXPECT_EQ(uint_spin->minimum(), 1);
  EXPECT_EQ(uint_spin->maximum(), 3);
  EXPECT_DOUBLE_EQ(double_spin->minimum(), -0.5);
  EXPECT_DOUBLE_EQ(double_spin->maximum(), 0.5);

  int_spin->setValue(999);
  EXPECT_EQ(int_spin->value(), 10);
  int_spin->setValue(-999);
  EXPECT_EQ(int_spin->value(), -10);

  uint_spin->setValue(0);
  EXPECT_EQ(uint_spin->value(), 1);
  uint_spin->setValue(999);
  EXPECT_EQ(uint_spin->value(), 3);

  double_spin->setValue(10.0);
  EXPECT_DOUBLE_EQ(double_spin->value(), 0.5);
  double_spin->setValue(-10.0);
  EXPECT_DOUBLE_EQ(double_spin->value(), -0.5);

  int_spin->setValue(9);
  int_spin->stepUp();
  EXPECT_EQ(int_spin->value(), 10);
  int_spin->stepUp();
  EXPECT_EQ(int_spin->value(), 10);

  uint_spin->setValue(2);
  uint_spin->stepUp();
  EXPECT_EQ(uint_spin->value(), 3);
  uint_spin->stepUp();
  EXPECT_EQ(uint_spin->value(), 3);

  double_spin->setValue(0.0);
  double_spin->stepUp();
  EXPECT_DOUBLE_EQ(double_spin->value(), 0.5);
  double_spin->stepDown();
  EXPECT_DOUBLE_EQ(double_spin->value(), -0.5);
  double_spin->stepDown();
  EXPECT_DOUBLE_EQ(double_spin->value(), -0.5);
}

TEST(StageParameterDialogTest,
     FrameMapRanges_DisplayedOneBasedAndStoredZeroBased) {
  (void)ensureApplication();

  std::vector<orc::ParameterDescriptor> descriptors;
  descriptors.push_back(makeDescriptor(
      "ranges", "Frame Ranges", orc::ParameterType::STRING, std::string("")));

  std::map<std::string, orc::ParameterValue> current_values;
  current_values["ranges"] = std::string("0-10,PAD_5,20-30");

  StageParameterDialog dialog("frame_map", "Frame Map", "", descriptors,
                              current_values);

  auto* edit =
      qobject_cast<QLineEdit*>(widgetForDisplayName(dialog, "Frame Ranges"));
  ASSERT_NE(edit, nullptr);

  // Stored 0-based value is displayed 1-based (matching the preview)
  EXPECT_EQ(edit->text().toStdString(), "1-11,PAD_5,21-31");

  // A 1-based edit is stored 0-based
  edit->setText("1-101,201");
  const auto values = dialog.get_values();
  ASSERT_TRUE(values.find("ranges") != values.end());
  ASSERT_TRUE(std::holds_alternative<std::string>(values.at("ranges")));
  EXPECT_EQ(std::get<std::string>(values.at("ranges")), "0-100,200");
}

TEST(StageParameterDialogTest,
     DropoutMapSpec_FrameValuesConvertedLineValuesUntouched) {
  (void)ensureApplication();

  std::vector<orc::ParameterDescriptor> descriptors;
  descriptors.push_back(makeDescriptor("dropout_map", "Dropout Map",
                                       orc::ParameterType::STRING,
                                       std::string("[]")));

  std::map<std::string, orc::ParameterValue> current_values;
  current_values["dropout_map"] =
      std::string("[{frame:0,add:[{line:10,start:100,end:200}]}]");

  StageParameterDialog dialog("dropout_map", "Dropout Map", "", descriptors,
                              current_values);

  auto* edit =
      qobject_cast<QLineEdit*>(widgetForDisplayName(dialog, "Dropout Map"));
  ASSERT_NE(edit, nullptr);

  EXPECT_EQ(edit->text().toStdString(),
            "[{frame:1,add:[{line:10,start:100,end:200}]}]");

  edit->setText("[{frame:42,add:[{line:5,start:1,end:2}]}]");
  const auto values = dialog.get_values();
  ASSERT_TRUE(std::holds_alternative<std::string>(values.at("dropout_map")));
  EXPECT_EQ(std::get<std::string>(values.at("dropout_map")),
            "[{frame:41,add:[{line:5,start:1,end:2}]}]");
}

TEST(StageParameterDialogTest,
     MaskLineSpec_LegacyUnparseableValuePassesThroughUnchanged) {
  (void)ensureApplication();

  std::vector<orc::ParameterDescriptor> descriptors;
  descriptors.push_back(makeDescriptor("lineSpec", "Line Specification",
                                       orc::ParameterType::STRING,
                                       std::string("")));

  std::map<std::string, orc::ParameterValue> current_values;
  current_values["lineSpec"] = std::string("F:20");

  StageParameterDialog dialog("mask_line", "Mask Line", "", descriptors,
                              current_values);

  auto* edit = qobject_cast<QLineEdit*>(
      widgetForDisplayName(dialog, "Line Specification"));
  ASSERT_NE(edit, nullptr);

  // Legacy specs that cannot be converted are shown verbatim...
  EXPECT_EQ(edit->text().toStdString(), "F:20");

  // ...and saved back untouched when the user does not modify them.
  const auto values = dialog.get_values();
  ASSERT_TRUE(std::holds_alternative<std::string>(values.at("lineSpec")));
  EXPECT_EQ(std::get<std::string>(values.at("lineSpec")), "F:20");
}

TEST(StageParameterDialogTest,
     NonSpecStringParameters_AreNotConvertedForOtherStages) {
  (void)ensureApplication();

  std::vector<orc::ParameterDescriptor> descriptors;
  descriptors.push_back(makeDescriptor(
      "ranges", "Frame Ranges", orc::ParameterType::STRING, std::string("")));

  std::map<std::string, orc::ParameterValue> current_values;
  current_values["ranges"] = std::string("0-10");

  // Same parameter name on a different stage: no conversion
  StageParameterDialog dialog("some_other_stage", "Other Stage", "",
                              descriptors, current_values);

  auto* edit =
      qobject_cast<QLineEdit*>(widgetForDisplayName(dialog, "Frame Ranges"));
  ASSERT_NE(edit, nullptr);
  EXPECT_EQ(edit->text().toStdString(), "0-10");

  const auto values = dialog.get_values();
  EXPECT_EQ(std::get<std::string>(values.at("ranges")), "0-10");
}

}  // namespace gui_unit_test
