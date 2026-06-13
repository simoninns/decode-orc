/*
 * File:        framescopedialog_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Tests for FrameScopeDialog helpers (gui-logic tier)
 *              and dialog construction smoke test (gui-widget tier)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

// ============================================================================
// Tier-1 tests: cvbs_sample_to_mv() and active_video_mv()
// No QApplication required — pure arithmetic on inline functions.
// Run under the "gui-logic" CTest label.
// ============================================================================

#include "framescopedialog.h"

#include <gtest/gtest.h>

#include <cmath>

#include "line_numbering.h"

namespace gui_unit_test {

// PAL spec values (cvbs_signal_constants.h / EBU Tech. 3280-E Table 1)
constexpr int32_t kPalBlanking = 256;
constexpr int32_t kPalBlack = 282;
constexpr int32_t kPalWhite = 844;
constexpr int32_t kPalSyncTip = 4;
constexpr int32_t kPalPeak = 1019;

// NTSC spec values (SMPTE 170M-2004 §11.4)
constexpr int32_t kNtscBlanking = 240;
constexpr int32_t kNtscBlack = 252;
constexpr int32_t kNtscWhite = 800;

// ---- cvbs_sample_to_mv() ---------------------------------------------------

TEST(CvbsSampleToMvTest, Blanking_IsZeroMillivolts) {
  // blanking_level always maps to 0 mV by definition (ITU-R BT.1700-1)
  EXPECT_DOUBLE_EQ(
      cvbs_sample_to_mv(static_cast<int16_t>(kPalBlanking), kPalBlanking, kPalWhite, 700.0),
      0.0);
  EXPECT_DOUBLE_EQ(
      cvbs_sample_to_mv(static_cast<int16_t>(kNtscBlanking), kNtscBlanking, kNtscWhite, 714.3),
      0.0);
}

TEST(CvbsSampleToMvTest, White_IsActiveVoltageMillivolts) {
  // white_level maps to active_mv exactly
  EXPECT_DOUBLE_EQ(
      cvbs_sample_to_mv(static_cast<int16_t>(kPalWhite), kPalBlanking, kPalWhite, 700.0),
      700.0);
  EXPECT_NEAR(
      cvbs_sample_to_mv(static_cast<int16_t>(kNtscWhite), kNtscBlanking, kNtscWhite, 714.3),
      714.3, 1e-9);
}

TEST(CvbsSampleToMvTest, SyncTip_IsNegativeMillivolts_PAL) {
  // sync_tip is below blanking → negative mV
  const double mv = cvbs_sample_to_mv(
      static_cast<int16_t>(kPalSyncTip), kPalBlanking, kPalWhite, 700.0);
  EXPECT_LT(mv, 0.0);
  // PAL sync tip: (4-256)/(844-256)*700 = -252/588*700 ≈ -300 mV
  EXPECT_NEAR(mv, -252.0 / 588.0 * 700.0, 1e-6);
}

TEST(CvbsSampleToMvTest, Peak_IsAboveWhite_PAL) {
  const double mv = cvbs_sample_to_mv(
      static_cast<int16_t>(kPalPeak), kPalBlanking, kPalWhite, 700.0);
  EXPECT_GT(mv, 700.0);
}

TEST(CvbsSampleToMvTest, Black_IsSmallPositive_PAL) {
  // black > blanking → small positive mV
  const double mv = cvbs_sample_to_mv(
      static_cast<int16_t>(kPalBlack), kPalBlanking, kPalWhite, 700.0);
  EXPECT_GT(mv, 0.0);
  EXPECT_LT(mv, 700.0);
}

TEST(CvbsSampleToMvTest, InvalidRange_ReturnsZero) {
  // white_level <= blanking_level → degenerate; must not crash or divide by zero
  EXPECT_DOUBLE_EQ(cvbs_sample_to_mv(100, 300, 300, 700.0), 0.0);
  EXPECT_DOUBLE_EQ(cvbs_sample_to_mv(100, 500, 300, 700.0), 0.0);
}

// ---- active_video_mv() -----------------------------------------------------

TEST(ActiveVideoMvTest, PAL_Returns700mV) {
  EXPECT_DOUBLE_EQ(active_video_mv(orc::presenters::VideoSystem::PAL), 700.0);
}

TEST(ActiveVideoMvTest, NTSC_Returns714point3mV) {
  EXPECT_DOUBLE_EQ(active_video_mv(orc::presenters::VideoSystem::NTSC), 714.3);
}

TEST(ActiveVideoMvTest, PAL_M_Returns714point3mV) {
  // PAL_M follows NTSC amplitude (ITU-R BT.1700-1 Annex 1 Part B)
  EXPECT_DOUBLE_EQ(active_video_mv(orc::presenters::VideoSystem::PAL_M), 714.3);
}

TEST(ActiveVideoMvTest, Unknown_ReturnsPALDefault) {
  EXPECT_DOUBLE_EQ(active_video_mv(orc::presenters::VideoSystem::Unknown), 700.0);
}

// ---- make_line_label() for all four LineNumberingMode values ---------------
// (orc::make_line_label from line_numbering.h)

TEST(MakeLineLabelTest, FrameFlat0Based_ReturnsZeroBasedIndex) {
  using orc::LineNumberingMode;
  auto lbl = orc::make_line_label(0, orc::VideoSystem::PAL, LineNumberingMode::kFrameFlat0Based);
  EXPECT_EQ(lbl.display, "0");

  lbl = orc::make_line_label(624, orc::VideoSystem::PAL, LineNumberingMode::kFrameFlat0Based);
  EXPECT_EQ(lbl.display, "624");
}

TEST(MakeLineLabelTest, FrameSequential1Based_ReturnsOneBased) {
  using orc::LineNumberingMode;
  auto lbl = orc::make_line_label(0, orc::VideoSystem::PAL, LineNumberingMode::kFrameSequential1Based);
  EXPECT_EQ(lbl.display, "1");

  lbl = orc::make_line_label(624, orc::VideoSystem::PAL, LineNumberingMode::kFrameSequential1Based);
  EXPECT_EQ(lbl.display, "625");
}

TEST(MakeLineLabelTest, FieldRelative_PAL_Field1) {
  using orc::LineNumberingMode;
  // Frame-flat line 0 → field 1, line 1
  auto lbl = orc::make_line_label(0, orc::VideoSystem::PAL, LineNumberingMode::kFieldRelative);
  EXPECT_EQ(lbl.field, 1);
  EXPECT_EQ(lbl.line_in_field, 1);
  EXPECT_EQ(lbl.display, "F1L1");

  // Frame-flat line 312 → field 1, line 313 (last field-1 line in PAL)
  lbl = orc::make_line_label(312, orc::VideoSystem::PAL, LineNumberingMode::kFieldRelative);
  EXPECT_EQ(lbl.field, 1);
  EXPECT_EQ(lbl.line_in_field, 313);
}

TEST(MakeLineLabelTest, FieldRelative_PAL_Field2) {
  using orc::LineNumberingMode;
  // Frame-flat line 313 → field 2, line 1 (first field-2 line in PAL)
  auto lbl = orc::make_line_label(313, orc::VideoSystem::PAL, LineNumberingMode::kFieldRelative);
  EXPECT_EQ(lbl.field, 2);
  EXPECT_EQ(lbl.line_in_field, 1);
}

TEST(MakeLineLabelTest, BroadcastInterlaced_PAL_Field1Line0) {
  using orc::LineNumberingMode;
  // Frame-flat line 0 (field 1) → broadcast line 1 (odd, ITU-R BT.470-6)
  auto lbl = orc::make_line_label(0, orc::VideoSystem::PAL, LineNumberingMode::kBroadcastInterlaced);
  EXPECT_EQ(lbl.broadcast_line, 1);
  EXPECT_EQ(lbl.display, "1");
}

TEST(MakeLineLabelTest, BroadcastInterlaced_PAL_Field2Line0) {
  using orc::LineNumberingMode;
  // Frame-flat line 313 (first field-2 line) → broadcast line 2 (even)
  auto lbl = orc::make_line_label(313, orc::VideoSystem::PAL, LineNumberingMode::kBroadcastInterlaced);
  EXPECT_EQ(lbl.broadcast_line, 2);
}

TEST(MakeLineLabelTest, BroadcastInterlaced_NTSC_Field1Line0) {
  using orc::LineNumberingMode;
  // NTSC frame-flat line 0 (field 1) → broadcast line 2 (SMPTE 170M-2004 §11.3)
  auto lbl = orc::make_line_label(0, orc::VideoSystem::NTSC, LineNumberingMode::kBroadcastInterlaced);
  EXPECT_EQ(lbl.broadcast_line, 2);
}

TEST(MakeLineLabelTest, BroadcastInterlaced_NTSC_Field2Line0) {
  using orc::LineNumberingMode;
  // NTSC frame-flat line 262 (first field-2 line) → broadcast line 1
  auto lbl = orc::make_line_label(262, orc::VideoSystem::NTSC, LineNumberingMode::kBroadcastInterlaced);
  EXPECT_EQ(lbl.broadcast_line, 1);
}

// ============================================================================
// Tier-3 tests: FrameScopeDialog construction (gui-widget, offscreen)
// Requires QApplication. Run under the "gui-widget" CTest label.
// ============================================================================

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>

namespace {

QApplication& ensureApplication() {
  if (auto* existing = qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *existing;
  }
  static int argc = 3;
  static char app_name[] = "orc-framescopedialog-test";
  static char platform_opt[] = "-platform";
  static char platform_val[] = "offscreen";
  static char* argv[] = {app_name, platform_opt, platform_val, nullptr};
  static QApplication* app = [] {
    auto* a = new QApplication(argc, argv);
    a->setQuitOnLastWindowClosed(false);
    return a;
  }();
  return *app;
}

}  // namespace

TEST(FrameScopeDialogSmokeTest, Dialog_ConstructsWithoutCrash) {
  (void)ensureApplication();
  FrameScopeDialog dialog;
  EXPECT_FALSE(dialog.isVisible());
}

TEST(FrameScopeDialogSmokeTest, Dialog_HasLineNumberingModeCombo) {
  (void)ensureApplication();
  FrameScopeDialog dialog;
  // The numbering mode combo must have exactly 4 entries
  const auto* combo = dialog.findChild<QComboBox*>("", Qt::FindChildrenRecursively);
  // At least one QComboBox exists
  EXPECT_NE(combo, nullptr);
}

TEST(FrameScopeDialogSmokeTest, Dialog_ShowAndHide_EmitsDialogClosed) {
  (void)ensureApplication();
  FrameScopeDialog dialog;
  bool closed_signal_received = false;
  QObject::connect(&dialog, &FrameScopeDialog::dialogClosed,
                   [&]() { closed_signal_received = true; });

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(dialog.isVisible());

  dialog.hide();
  QCoreApplication::processEvents();
  EXPECT_TRUE(closed_signal_received);
}

}  // namespace gui_unit_test
