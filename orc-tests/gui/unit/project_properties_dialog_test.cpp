/*
 * File:        project_properties_dialog_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Offscreen smoke tests for ProjectPropertiesDialog amplitude
 *              unit round-trip
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>

#include "projectpropertiesdialog.h"

namespace gui_unit_test {
namespace {

QApplication& ensureApplication() {
  if (auto* existing_app =
          qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *existing_app;
  }

  static int argc = 3;
  static char app_name[] = "orc-gui-project-properties-dialog-test";
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

TEST(ProjectPropertiesDialogTest, AmplitudeUnit_SetIRE_RoundTrips) {
  (void)ensureApplication();
  ProjectPropertiesDialog dialog;
  dialog.setAmplitudeUnit(orc::AmplitudeDisplayUnit::IRE);
  EXPECT_EQ(dialog.amplitudeUnit(), orc::AmplitudeDisplayUnit::IRE);
}

TEST(ProjectPropertiesDialogTest, AmplitudeUnit_SetMillivolts_RoundTrips) {
  (void)ensureApplication();
  ProjectPropertiesDialog dialog;
  dialog.setAmplitudeUnit(orc::AmplitudeDisplayUnit::Millivolts);
  EXPECT_EQ(dialog.amplitudeUnit(), orc::AmplitudeDisplayUnit::Millivolts);
}

TEST(ProjectPropertiesDialogTest, AmplitudeUnit_SetSamples10Bit_RoundTrips) {
  (void)ensureApplication();
  ProjectPropertiesDialog dialog;
  dialog.setAmplitudeUnit(orc::AmplitudeDisplayUnit::Samples10Bit);
  EXPECT_EQ(dialog.amplitudeUnit(), orc::AmplitudeDisplayUnit::Samples10Bit);
}

}  // namespace gui_unit_test
