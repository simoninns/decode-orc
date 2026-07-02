/*
 * File:        analysis_dialog_smoke_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Offscreen smoke tests for analysis dialog subclasses (Phase 9)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <algorithm>

#include "burstlevelanalysisdialog.h"
#include "dropoutanalysisdialog.h"
#include "framescopedialog.h"
#include "frametimingdialog.h"
#include "ntscobserverdialog.h"
#include "snranalysisdialog.h"
#include "vbidialog.h"
#include "videoparameterobserverdialog.h"

// GenericAnalysisDialog and DropoutEditorDialog are intentionally omitted:
// - GenericAnalysisDialog requires a live AnalysisPresenter* and
// AnalysisToolInfo, which
//   cannot be constructed without a running pipeline. An integration-level test
//   would be appropriate once a mock AnalysisPresenter seam is extracted.
// - DropoutEditorDialog requires a DropoutPresenter* and a shared_ptr field
// representation,
//   which similarly require presenter infrastructure beyond the scope of
//   offscreen smoke tests.

namespace gui_unit_test {

namespace {

QApplication& ensureApplication() {
  if (auto* existing_app =
          qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *existing_app;
  }

  static int argc = 3;
  static char app_name[] = "orc-gui-analysis-dialog-smoke-test";
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

}  // namespace

// ---------------------------------------------------------------------------
// AnalysisDialogBase subclasses
// ---------------------------------------------------------------------------

TEST(AnalysisDialogSmokeTest, BurstLevelAnalysisDialog_CanShowAndClose) {
  (void)ensureApplication();

  BurstLevelAnalysisDialog dialog;

  dialog.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(dialog.isVisible());

  dialog.close();
  QCoreApplication::processEvents();

  EXPECT_FALSE(dialog.isVisible());
}

TEST(AnalysisDialogSmokeTest, DropoutAnalysisDialog_CanShowAndClose) {
  (void)ensureApplication();

  DropoutAnalysisDialog dialog;

  dialog.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(dialog.isVisible());

  dialog.close();
  QCoreApplication::processEvents();

  EXPECT_FALSE(dialog.isVisible());
}

TEST(AnalysisDialogSmokeTest, SnrAnalysisDialog_CanShowAndClose) {
  (void)ensureApplication();

  SNRAnalysisDialog dialog;

  dialog.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(dialog.isVisible());

  dialog.close();
  QCoreApplication::processEvents();

  EXPECT_FALSE(dialog.isVisible());
}

// ---------------------------------------------------------------------------
// Analysis and observation dialogs (simple QWidget* parent constructors)
// ---------------------------------------------------------------------------

TEST(AnalysisDialogSmokeTest, NtscObserverDialog_CanShowAndClose) {
  (void)ensureApplication();

  NtscObserverDialog dialog;

  dialog.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(dialog.isVisible());

  dialog.close();
  QCoreApplication::processEvents();

  EXPECT_FALSE(dialog.isVisible());
}

TEST(AnalysisDialogSmokeTest, VideoParameterObserverDialog_CanShowAndClose) {
  (void)ensureApplication();

  VideoParameterObserverDialog dialog;

  dialog.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(dialog.isVisible());

  dialog.close();
  QCoreApplication::processEvents();

  EXPECT_FALSE(dialog.isVisible());
}

namespace {

orc::presenters::VideoParameterObservationView makeBurstObservation() {
  orc::presenters::VideoParameterObservationView obs;
  orc::presenters::VideoParametersView vp;
  vp.system = orc::presenters::VideoSystem::PAL;
  vp.blanking_level = 256;
  vp.white_level = 844;
  obs.video_params = vp;
  obs.burst_level_10bit = 100.0;
  return obs;
}

// True if any label in the dialog shows the given text.
bool dialogShowsText(const QDialog& dialog, const QString& text) {
  const auto labels = dialog.findChildren<QLabel*>();
  return std::any_of(labels.cbegin(), labels.cend(),
                     [&text](const QLabel* l) { return l->text() == text; });
}

}  // namespace

TEST(AnalysisDialogSmokeTest,
     VideoParameterObserverDialog_BurstLevelFollowsAmplitudeUnit) {
  (void)ensureApplication();

  VideoParameterObserverDialog dialog;
  const orc::FieldID field_id(0);
  const auto obs = makeBurstObservation();

  // 10-bit: raw ADU value.
  dialog.setAmplitudeUnit(orc::AmplitudeDisplayUnit::Samples10Bit);
  dialog.updateObservations(field_id, obs);
  EXPECT_TRUE(dialogShowsText(dialog, "100.0 ADU"));

  // IRE: AC amplitude scaled by blanking-to-white range (588 ADU = 100 IRE).
  dialog.setAmplitudeUnit(orc::AmplitudeDisplayUnit::IRE);
  dialog.updateObservations(field_id, obs);
  EXPECT_TRUE(dialogShowsText(dialog, "17.0 IRE"));

  // mV: PAL active video is 700 mV.
  dialog.setAmplitudeUnit(orc::AmplitudeDisplayUnit::Millivolts);
  dialog.updateObservations(field_id, obs);
  EXPECT_TRUE(dialogShowsText(dialog, "119.0 mV"));
}

TEST(AnalysisDialogSmokeTest,
     VideoParameterObserverDialog_BurstLevelFallsBackToAduWithoutLevels) {
  (void)ensureApplication();

  VideoParameterObserverDialog dialog;
  auto obs = makeBurstObservation();
  obs.video_params->blanking_level = -1;  // Unmigrated source: no levels.

  dialog.setAmplitudeUnit(orc::AmplitudeDisplayUnit::IRE);
  dialog.updateObservations(orc::FieldID(0), obs);
  EXPECT_TRUE(dialogShowsText(dialog, "100.0 ADU"));
}

TEST(AnalysisDialogSmokeTest, VbiDialog_CanShowAndClose) {
  (void)ensureApplication();

  VBIDialog dialog;

  dialog.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(dialog.isVisible());

  dialog.close();
  QCoreApplication::processEvents();

  EXPECT_FALSE(dialog.isVisible());
}

TEST(AnalysisDialogSmokeTest, FrameScopeDialog_CanShowAndClose) {
  (void)ensureApplication();

  FrameScopeDialog dialog;

  dialog.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(dialog.isVisible());

  dialog.close();
  QCoreApplication::processEvents();

  EXPECT_FALSE(dialog.isVisible());
}

TEST(AnalysisDialogSmokeTest, FrameTimingDialog_CanShowAndClose) {
  (void)ensureApplication();

  FrameTimingDialog dialog;

  dialog.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(dialog.isVisible());

  dialog.close();
  QCoreApplication::processEvents();

  EXPECT_FALSE(dialog.isVisible());
}

}  // namespace gui_unit_test
