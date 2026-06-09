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

TEST(GUIProjectTest, newEmptyProjectDelegatesLifecycleAndClearsModified) {
  auto mock = std::make_unique<
      StrictMock<orc::presenters::test::MockProjectPresenter>>();
  auto* mock_presenter = mock.get();
  GUIProject project(std::move(mock));

  EXPECT_CALL(*mock_presenter, clearProject()).Times(1);
  EXPECT_CALL(*mock_presenter, setProjectName("phase3-project")).Times(1);
  EXPECT_CALL(*mock_presenter,
              setVideoFormat(orc::presenters::VideoFormat::NTSC))
      .Times(1);
  EXPECT_CALL(*mock_presenter,
              setSourceType(orc::presenters::SourceType::Composite))
      .Times(1);
  EXPECT_CALL(*mock_presenter, clearModifiedFlag()).Times(1);

  QString error;
  EXPECT_TRUE(project.newEmptyProject(
      "phase3-project", orc::presenters::VideoFormat::NTSC,
      orc::presenters::SourceType::Composite, &error));
  EXPECT_TRUE(error.isEmpty());
}

TEST(GUIProjectTest, isModifiedDelegatesToPresenter) {
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

TEST(GUIProjectTest, saveToFileDelegatesToPresenterAndStoresPath) {
  auto mock = std::make_unique<
      StrictMock<orc::presenters::test::MockProjectPresenter>>();
  auto* mock_presenter = mock.get();
  GUIProject project(std::move(mock));

  EXPECT_CALL(*mock_presenter, saveProject("/tmp/phase3-save.orcprj"))
      .WillOnce(Return(true));

  QString error;
  EXPECT_TRUE(project.saveToFile("/tmp/phase3-save.orcprj", &error));
  EXPECT_TRUE(error.isEmpty());
  EXPECT_EQ(project.projectPath(), QString("/tmp/phase3-save.orcprj"));
}

TEST(GUIProjectTest, loadFromFileDelegatesToPresenterBuildsDagAndStoresPath) {
  auto mock = std::make_unique<
      StrictMock<orc::presenters::test::MockProjectPresenter>>();
  auto* mock_presenter = mock.get();
  GUIProject project(std::move(mock));

  EXPECT_CALL(*mock_presenter, loadProject("/tmp/phase3-load.orcprj"))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_presenter, buildDAG())
      .WillOnce(Return(std::make_shared<int>(42)));
  EXPECT_CALL(*mock_presenter, getNodes())
      .WillOnce(Return(std::vector<orc::presenters::NodeInfo>{}));
  EXPECT_CALL(*mock_presenter, listAllStages())
      .WillOnce(Return(std::vector<orc::presenters::StageInfo>{}));

  QString error;
  EXPECT_TRUE(project.loadFromFile("/tmp/phase3-load.orcprj", &error));
  EXPECT_TRUE(error.isEmpty());
  EXPECT_EQ(project.projectPath(), QString("/tmp/phase3-load.orcprj"));
}

TEST(GUIProjectTest, clearResetsPathAndDelegatesProjectReset) {
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
