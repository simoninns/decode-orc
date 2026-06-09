/*
 * File:        render_coordinator_test.cpp
 * Module:      orc-tests/gui/unit
 * Purpose:     Actor-style tests for RenderCoordinator request/response
 * behavior
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "render_coordinator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QMetaType>
#include <QSignalSpy>
#include <QThread>
#include <chrono>
#include <thread>

#include "mocks/mock_render_presenter.h"

Q_DECLARE_METATYPE(orc::PreviewRenderResult)

namespace gui_unit_test {

using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

static bool registerRenderCoordinatorMetatypes() {
  qRegisterMetaType<orc::PreviewRenderResult>("orc::PreviewRenderResult");
  return true;
}

static const bool kMetatypesRegistered = registerRenderCoordinatorMetatypes();

static bool waitForCount(QSignalSpy& spy, int expected, int timeout_ms = 2000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    QCoreApplication::processEvents();
    if (spy.count() >= expected) {
      return true;
    }
    QThread::msleep(5);
  }
  return spy.count() >= expected;
}

TEST(RenderCoordinatorTest, triggerRequestRoundTripEmitsTriggerComplete) {
  (void)kMetatypesRegistered;

  auto mock_presenter =
      std::make_shared<NiceMock<orc::presenters::test::MockRenderPresenter>>();

  EXPECT_CALL(*mock_presenter, setDAG(testing::_)).Times(1);
  EXPECT_CALL(*mock_presenter, setShowDropouts(false)).Times(1);
  EXPECT_CALL(*mock_presenter, triggerStage(orc::NodeID(7), testing::_))
      .WillOnce(Return(1001));

  RenderCoordinator coordinator(
      [mock_presenter](
          void*) -> std::shared_ptr<orc::presenters::IRenderPresenter> {
        return mock_presenter;
      });

  QSignalSpy trigger_complete_spy(&coordinator,
                                  &RenderCoordinator::triggerComplete);

  coordinator.start();
  coordinator.setProject(reinterpret_cast<void*>(0x1));
  coordinator.updateDAG(std::make_shared<int>(123));

  const uint64_t request_id = coordinator.requestTrigger(orc::NodeID(7));

  ASSERT_TRUE(waitForCount(trigger_complete_spy, 1));
  ASSERT_EQ(trigger_complete_spy.count(), 1);
  EXPECT_EQ(trigger_complete_spy.at(0).at(0).toULongLong(), request_id);
  EXPECT_TRUE(trigger_complete_spy.at(0).at(1).toBool());

  coordinator.stop();
}

TEST(RenderCoordinatorTest, workerLifecycleStartStopIsStable) {
  (void)kMetatypesRegistered;

  auto mock_presenter =
      std::make_shared<NiceMock<orc::presenters::test::MockRenderPresenter>>();

  RenderCoordinator coordinator(
      [mock_presenter](
          void*) -> std::shared_ptr<orc::presenters::IRenderPresenter> {
        return mock_presenter;
      });

  coordinator.start();
  coordinator.stop();

  coordinator.start();
  coordinator.stop();
}

TEST(RenderCoordinatorTest, triggerRequestsAreProcessedInOrder) {
  (void)kMetatypesRegistered;

  auto mock_presenter =
      std::make_shared<NiceMock<orc::presenters::test::MockRenderPresenter>>();

  EXPECT_CALL(*mock_presenter, setDAG(testing::_)).Times(1);
  EXPECT_CALL(*mock_presenter, setShowDropouts(false)).Times(1);

  {
    InSequence seq;
    EXPECT_CALL(*mock_presenter, triggerStage(orc::NodeID(1), testing::_))
        .WillOnce(Invoke(
            [](orc::NodeID,
               orc::presenters::IRenderPresenter::TriggerProgressCallback) {
              std::this_thread::sleep_for(std::chrono::milliseconds(15));
              return 2001ULL;
            }));
    EXPECT_CALL(*mock_presenter, triggerStage(orc::NodeID(2), testing::_))
        .WillOnce(Return(2002ULL));
  }

  RenderCoordinator coordinator(
      [mock_presenter](
          void*) -> std::shared_ptr<orc::presenters::IRenderPresenter> {
        return mock_presenter;
      });

  QSignalSpy trigger_complete_spy(&coordinator,
                                  &RenderCoordinator::triggerComplete);

  coordinator.start();
  coordinator.setProject(reinterpret_cast<void*>(0x1));
  coordinator.updateDAG(std::make_shared<int>(234));

  const uint64_t first_id = coordinator.requestTrigger(orc::NodeID(1));
  const uint64_t second_id = coordinator.requestTrigger(orc::NodeID(2));

  ASSERT_TRUE(waitForCount(trigger_complete_spy, 2));
  ASSERT_EQ(trigger_complete_spy.count(), 2);

  EXPECT_EQ(trigger_complete_spy.at(0).at(0).toULongLong(), first_id);
  EXPECT_EQ(trigger_complete_spy.at(1).at(0).toULongLong(), second_id);

  coordinator.stop();
}

TEST(RenderCoordinatorTest, stalePreviewResponsesAreSuppressed) {
  (void)kMetatypesRegistered;

  auto mock_presenter =
      std::make_shared<NiceMock<orc::presenters::test::MockRenderPresenter>>();

  EXPECT_CALL(*mock_presenter, setDAG(testing::_)).Times(1);
  EXPECT_CALL(*mock_presenter, setShowDropouts(false)).Times(1);

  EXPECT_CALL(
      *mock_presenter,
      renderPreview(orc::NodeID(9), orc::PreviewOutputType::Field, 0, ""))
      .WillOnce(
          Invoke([](orc::NodeID node_id, orc::PreviewOutputType output_type,
                    uint64_t output_index, const std::string&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return orc::PreviewRenderResult{
                {}, true, "", node_id, output_type, output_index, std::nullopt};
          }))
      .WillOnce(
          Invoke([](orc::NodeID node_id, orc::PreviewOutputType output_type,
                    uint64_t output_index, const std::string&) {
            return orc::PreviewRenderResult{
                {}, true, "", node_id, output_type, output_index, std::nullopt};
          }));

  RenderCoordinator coordinator(
      [mock_presenter](
          void*) -> std::shared_ptr<orc::presenters::IRenderPresenter> {
        return mock_presenter;
      });

  QSignalSpy preview_spy(&coordinator, &RenderCoordinator::previewReady);

  coordinator.start();
  coordinator.setProject(reinterpret_cast<void*>(0x1));
  coordinator.updateDAG(std::make_shared<int>(345));

  const uint64_t first_id = coordinator.requestPreview(
      orc::NodeID(9), orc::PreviewOutputType::Field, 0);
  const uint64_t second_id = coordinator.requestPreview(
      orc::NodeID(9), orc::PreviewOutputType::Field, 0);

  ASSERT_TRUE(waitForCount(preview_spy, 1));
  EXPECT_EQ(preview_spy.count(), 1);
  EXPECT_EQ(preview_spy.at(0).at(0).toULongLong(), second_id);
  EXPECT_NE(first_id, second_id);

  coordinator.stop();
}

TEST(RenderCoordinatorTest,
     applyStageParametersRequestEmitsPreviewReadyForSameRequestId) {
  (void)kMetatypesRegistered;

  auto mock_presenter =
      std::make_shared<NiceMock<orc::presenters::test::MockRenderPresenter>>();

  EXPECT_CALL(*mock_presenter, setDAG(testing::_)).Times(1);
  EXPECT_CALL(*mock_presenter, setShowDropouts(false)).Times(1);

  EXPECT_CALL(
      *mock_presenter,
      applyStageParameters(
          orc::NodeID(11),
          testing::A<const std::map<std::string, orc::ParameterValue>&>()))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *mock_presenter,
      renderPreview(orc::NodeID(11), orc::PreviewOutputType::Field, 2, ""))
      .WillOnce(
          Invoke([](orc::NodeID node_id, orc::PreviewOutputType output_type,
                    uint64_t output_index, const std::string&) {
            return orc::PreviewRenderResult{
                {}, true, "", node_id, output_type, output_index, std::nullopt};
          }));

  RenderCoordinator coordinator(
      [mock_presenter](
          void*) -> std::shared_ptr<orc::presenters::IRenderPresenter> {
        return mock_presenter;
      });

  QSignalSpy preview_spy(&coordinator, &RenderCoordinator::previewReady);

  coordinator.start();
  coordinator.setProject(reinterpret_cast<void*>(0x1));
  coordinator.updateDAG(std::make_shared<int>(456));

  std::map<std::string, orc::ParameterValue> params;
  params.emplace("phase", int32_t{3});
  const uint64_t request_id = coordinator.requestApplyStageParameters(
      orc::NodeID(11), orc::PreviewOutputType::Field, 2, "", std::move(params));

  ASSERT_TRUE(waitForCount(preview_spy, 1));
  ASSERT_EQ(preview_spy.count(), 1);
  EXPECT_EQ(preview_spy.at(0).at(0).toULongLong(), request_id);

  coordinator.stop();
}

}  // namespace gui_unit_test
