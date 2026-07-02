/*
 * File:        gui_project_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Unit tests for GUIProject model behavior through
 * IProjectPresenter seam
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QString>

#include "guiproject.h"
#include "mocks/mock_project_presenter.h"

namespace gui_unit_test {

using ::testing::Return;
using ::testing::StrictMock;

TEST(GUIProjectTest, NewEmptyProject_DelegatesLifecycleAndClearsModified) {
  auto mock = std::make_unique<
      StrictMock<orc::presenters::test::MockProjectPresenter>>();
  auto* mock_presenter = mock.get();
  GUIProject project(std::move(mock));

  EXPECT_CALL(*mock_presenter, clearProject()).Times(1);
  EXPECT_CALL(*mock_presenter, setProjectName("test-project")).Times(1);
  EXPECT_CALL(*mock_presenter,
              setVideoFormat(orc::presenters::VideoFormat::NTSC))
      .Times(1);
  EXPECT_CALL(*mock_presenter,
              setSourceType(orc::presenters::SourceType::Composite))
      .Times(1);
  // NTSC projects default to IRE (SMPTE 170M-2004 convention).
  EXPECT_CALL(*mock_presenter, setAmplitudeUnit(orc::AmplitudeDisplayUnit::IRE))
      .Times(1);
  EXPECT_CALL(*mock_presenter, clearModifiedFlag()).Times(1);

  QString error;
  EXPECT_TRUE(project.newEmptyProject(
      "test-project", orc::presenters::VideoFormat::NTSC,
      orc::presenters::SourceType::Composite, &error));
  EXPECT_TRUE(error.isEmpty());
}

TEST(GUIProjectTest, IsModified_DelegatesToPresenter) {
  auto mock = std::make_unique<
      StrictMock<orc::presenters::test::MockProjectPresenter>>();
  auto* mock_presenter = mock.get();
  GUIProject project(std::move(mock));

  EXPECT_CALL(*mock_presenter, isModified()).WillOnce(Return(true));
  EXPECT_TRUE(project.isModified());

  // Compatibility no-op: GUIProject does not directly push modified state into
  // presenter.
  project.setModified(false);
}

TEST(GUIProjectTest, SaveToFile_DelegatesToPresenterAndStoresPath) {
  auto mock = std::make_unique<
      StrictMock<orc::presenters::test::MockProjectPresenter>>();
  auto* mock_presenter = mock.get();
  GUIProject project(std::move(mock));

  EXPECT_CALL(*mock_presenter, saveProject("/tmp/test-save.orcprj"))
      .WillOnce(Return(true));

  QString error;
  EXPECT_TRUE(project.saveToFile("/tmp/test-save.orcprj", &error));
  EXPECT_TRUE(error.isEmpty());
  EXPECT_EQ(project.projectPath(), QString("/tmp/test-save.orcprj"));
}

TEST(GUIProjectTest, LoadFromFile_DelegatesToPresenterBuildsDagAndStoresPath) {
  auto mock = std::make_unique<
      StrictMock<orc::presenters::test::MockProjectPresenter>>();
  auto* mock_presenter = mock.get();
  GUIProject project(std::move(mock));

  EXPECT_CALL(*mock_presenter, loadProject("/tmp/test-load.orcprj"))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_presenter, buildDAG())
      .WillOnce(Return(std::make_shared<int>(42)));
  EXPECT_CALL(*mock_presenter, getNodes())
      .WillOnce(Return(std::vector<orc::presenters::NodeInfo>{}));
  EXPECT_CALL(*mock_presenter, listAllStages())
      .WillOnce(Return(std::vector<orc::presenters::StageInfo>{}));

  QString error;
  EXPECT_TRUE(project.loadFromFile("/tmp/test-load.orcprj", &error));
  EXPECT_TRUE(error.isEmpty());
  EXPECT_EQ(project.projectPath(), QString("/tmp/test-load.orcprj"));
}

TEST(GUIProjectTest, Clear_ResetsPathAndDelegatesProjectReset) {
  auto mock = std::make_unique<
      StrictMock<orc::presenters::test::MockProjectPresenter>>();
  auto* mock_presenter = mock.get();
  GUIProject project(std::move(mock));

  project.setProjectPath("/tmp/will-be-cleared.orcprj");

  EXPECT_CALL(*mock_presenter, clearProject()).Times(1);
  EXPECT_CALL(*mock_presenter, clearModifiedFlag()).Times(1);

  project.clear();

  EXPECT_TRUE(project.projectPath().isEmpty());
}

}  // namespace gui_unit_test
