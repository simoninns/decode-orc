/*
 * File:        waveform_monitor_dialog_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Tests for the waveform monitor's field selector — the buffer
 *              slice it produces and the trace the dialog then draws
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QImage>
#include <QPixmap>
#include <cstdint>
#include <optional>
#include <vector>

#include "waveformmonitordialog.h"
#include "waveformmonitorwidget.h"

namespace gui_unit_test {
namespace {

QApplication& ensureApplication() {
  if (auto* existing_app =
          qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *existing_app;
  }

  static int argc = 3;
  static char app_name[] = "orc-gui-waveform-monitor-dialog-test";
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

// ---------------------------------------------------------------------------
// Tier 1: the field slice itself
// ---------------------------------------------------------------------------

constexpr int kSamplesPerLine = 4;

// A flat buffer whose every sample carries its own field's level, so the
// slice can be checked by value rather than by offset arithmetic.
std::vector<int16_t> twoFieldBuffer(int field1_lines, int16_t field1_level,
                                    int field2_lines, int16_t field2_level) {
  std::vector<int16_t> samples;
  samples.reserve(static_cast<size_t>(field1_lines + field2_lines) *
                  kSamplesPerLine);
  samples.insert(samples.end(),
                 static_cast<size_t>(field1_lines) * kSamplesPerLine,
                 field1_level);
  samples.insert(samples.end(),
                 static_cast<size_t>(field2_lines) * kSamplesPerLine,
                 field2_level);
  return samples;
}

TEST(WaveformMonitorFieldSliceTest, Field1_KeepsTheLeadingFieldOnly) {
  int f1 = 3;
  int f2 = 2;
  const std::vector<int16_t> samples = twoFieldBuffer(f1, 100, f2, 200);

  const std::vector<int16_t> sliced = WaveformMonitorDialog::sliceToField(
      samples, f1, f2, WaveformFieldSelection::Field1);

  EXPECT_EQ(f1, 3);
  EXPECT_EQ(f2, 0);
  ASSERT_EQ(sliced.size(), static_cast<size_t>(3 * kSamplesPerLine));
  EXPECT_EQ(sliced.front(), 100);
  EXPECT_EQ(sliced.back(), 100);
}

TEST(WaveformMonitorFieldSliceTest, Field2_KeepsTheTrailingFieldOnly) {
  int f1 = 3;
  int f2 = 2;
  const std::vector<int16_t> samples = twoFieldBuffer(f1, 100, f2, 200);

  const std::vector<int16_t> sliced = WaveformMonitorDialog::sliceToField(
      samples, f1, f2, WaveformFieldSelection::Field2);

  // The kept field becomes the buffer's only field.
  EXPECT_EQ(f1, 2);
  EXPECT_EQ(f2, 0);
  ASSERT_EQ(sliced.size(), static_cast<size_t>(2 * kSamplesPerLine));
  EXPECT_EQ(sliced.front(), 200);
  EXPECT_EQ(sliced.back(), 200);
}

TEST(WaveformMonitorFieldSliceTest, Frame_LeavesTheBufferAndHeightsAlone) {
  int f1 = 3;
  int f2 = 2;
  const std::vector<int16_t> samples = twoFieldBuffer(f1, 100, f2, 200);

  const std::vector<int16_t> sliced = WaveformMonitorDialog::sliceToField(
      samples, f1, f2, WaveformFieldSelection::Frame);

  EXPECT_EQ(f1, 3);
  EXPECT_EQ(f2, 2);
  EXPECT_EQ(sliced, samples);
}

// Single-field preview output arrives with no second field; asking for one
// must not read past the end of the buffer.
TEST(WaveformMonitorFieldSliceTest, Field2_OfSingleFieldData_IsLeftUnchanged) {
  int f1 = 4;
  int f2 = 0;
  const std::vector<int16_t> samples = twoFieldBuffer(f1, 100, f2, 0);

  const std::vector<int16_t> sliced = WaveformMonitorDialog::sliceToField(
      samples, f1, f2, WaveformFieldSelection::Field2);

  EXPECT_EQ(f1, 4);
  EXPECT_EQ(f2, 0);
  EXPECT_EQ(sliced, samples);
}

TEST(WaveformMonitorFieldSliceTest, EmptyBuffer_IsLeftUnchanged) {
  int f1 = 0;
  int f2 = 0;
  const std::vector<int16_t> samples;

  const std::vector<int16_t> sliced = WaveformMonitorDialog::sliceToField(
      samples, f1, f2, WaveformFieldSelection::Field1);

  EXPECT_EQ(f1, 0);
  EXPECT_EQ(f2, 0);
  EXPECT_TRUE(sliced.empty());
}

// ---------------------------------------------------------------------------
// Tier 3: the dialog's selector, offscreen
// ---------------------------------------------------------------------------

// Enough lines per field that the VBI lines stripped in the default
// active-video range still leave a picture behind.
constexpr int kFieldLines = 30;
constexpr int kFrameSamplesPerLine = 40;

orc::presenters::VideoParametersView ntscParameters() {
  orc::presenters::VideoParametersView params;
  params.system = orc::presenters::VideoSystem::NTSC;
  params.frame_width_nominal = kFrameSamplesPerLine;
  params.active_video_start = 4;
  params.active_video_end = kFrameSamplesPerLine - 1;
  params.sync_tip_level = 0;
  params.blanking_level = 240;
  params.black_level = 280;
  params.white_level = 800;
  params.peak_level = 1000;
  return params;
}

// Each field flat at its own level, so which field is being accumulated shows
// up as the height of the trace.
std::vector<int16_t> frameWithFieldLevels(int16_t field1_level,
                                          int16_t field2_level) {
  std::vector<int16_t> samples;
  const size_t field_size =
      static_cast<size_t>(kFieldLines) * kFrameSamplesPerLine;
  samples.insert(samples.end(), field_size, field1_level);
  samples.insert(samples.end(), field_size, field2_level);
  return samples;
}

QComboBox* fieldCombo(WaveformMonitorDialog& dialog) {
  return dialog.findChild<QComboBox*>("waveform_field_combo");
}

// Fed as a separate luma channel so the dialog's default Y-only view uses the
// samples as supplied; a composite source would go through the subcarrier
// notch filter first, whose buffer-edge taps are not what these tests are
// about.
void feedFrame(WaveformMonitorDialog& dialog, int16_t field1_level,
               int16_t field2_level) {
  dialog.setData(
      {}, frameWithFieldLevels(field1_level, field2_level), {}, kFieldLines,
      kFieldLines,
      std::optional<orc::presenters::VideoParametersView>(ntscParameters()));
}

// Only the trace widget, never the whole dialog: the combo box paints the
// name of the current selection, which would differ between two renders even
// if the trace did not.
QImage traceOf(WaveformMonitorDialog& dialog) {
  QCoreApplication::processEvents();
  auto* monitor = dialog.findChild<WaveformMonitorWidget*>();
  if (!monitor) return QImage();
  return monitor->grab().toImage();
}

TEST(WaveformMonitorDialogTest, OffersFrameAndBothFields) {
  ensureApplication();

  WaveformMonitorDialog dialog;
  QComboBox* combo = fieldCombo(dialog);
  ASSERT_NE(combo, nullptr);
  ASSERT_EQ(combo->count(), 3);
  EXPECT_EQ(combo->itemText(0), "Frame");
  EXPECT_EQ(combo->itemText(1), "Field 1");
  EXPECT_EQ(combo->itemText(2), "Field 2");
  EXPECT_EQ(combo->currentIndex(), 0);
}

TEST(WaveformMonitorDialogTest, DrawsADifferentTraceForEachField) {
  ensureApplication();

  WaveformMonitorDialog dialog;
  dialog.resize(600, 400);
  dialog.show();
  QComboBox* combo = fieldCombo(dialog);
  ASSERT_NE(combo, nullptr);

  // 300 and 780 are far enough apart in mV to land in different plot rows.
  feedFrame(dialog, 300, 780);
  const QImage frame_view = traceOf(dialog);
  ASSERT_FALSE(frame_view.isNull());

  combo->setCurrentIndex(1);
  const QImage field1_view = traceOf(dialog);

  combo->setCurrentIndex(2);
  const QImage field2_view = traceOf(dialog);

  ASSERT_EQ(frame_view.size(), field1_view.size());
  ASSERT_EQ(field1_view.size(), field2_view.size());
  EXPECT_NE(field1_view, field2_view)
      << "both fields drew the same trace, so the selection did nothing";
  EXPECT_NE(frame_view, field1_view);
  EXPECT_NE(frame_view, field2_view);
}

// The two fields of a frame that carries the same signal in both must draw
// the same trace: the selector picks lines, it does not change the mapping.
TEST(WaveformMonitorDialogTest, DrawsBothFieldsAlikeWhenTheyCarryOneLevel) {
  ensureApplication();

  WaveformMonitorDialog dialog;
  dialog.resize(600, 400);
  dialog.show();
  QComboBox* combo = fieldCombo(dialog);
  ASSERT_NE(combo, nullptr);

  feedFrame(dialog, 500, 500);

  combo->setCurrentIndex(1);
  const QImage field1_view = traceOf(dialog);

  combo->setCurrentIndex(2);
  const QImage field2_view = traceOf(dialog);

  EXPECT_EQ(field1_view, field2_view);
}

TEST(WaveformMonitorDialogTest, DisablesTheSelectorForSingleFieldData) {
  ensureApplication();

  WaveformMonitorDialog dialog;
  dialog.resize(600, 400);
  dialog.show();
  QComboBox* combo = fieldCombo(dialog);
  ASSERT_NE(combo, nullptr);

  feedFrame(dialog, 300, 780);
  EXPECT_TRUE(combo->isEnabled());
  combo->setCurrentIndex(2);

  // A single-field preview supplies no second field.
  std::vector<int16_t> one_field(
      static_cast<size_t>(kFieldLines) * kFrameSamplesPerLine, 500);
  dialog.setData(
      {}, std::move(one_field), {}, kFieldLines, 0,
      std::optional<orc::presenters::VideoParametersView>(ntscParameters()));

  EXPECT_FALSE(combo->isEnabled());
  EXPECT_EQ(combo->currentIndex(), 0)
      << "a stale field selection was left showing for single-field data";
}

}  // namespace
}  // namespace gui_unit_test
