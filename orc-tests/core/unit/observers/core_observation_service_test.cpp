/*
 * File:        core_observation_service_test.cpp
 * Module:      orc-tests/core/unit/observers
 * Purpose:     Unit tests for CoreObservationService (host observation service)
 *
 * Covers id lookup, unknown-id failure, available_observers() completeness,
 * and result delivery through a mocked IObservationContext. No filesystem,
 * network, or clock access — a mocked VideoFrameRepresentation drives the
 * observer deterministically.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "core_observation_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <orc/stage/field_id.h>
#include <orc/stage/frame_id.h>
#include <orc/stage/observation/observation_service_interface.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "../include/observation_context_interface_mock.h"
#include "../mocks/mock_video_frame_representation.h"

namespace orc {
namespace tests {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// The complete set of observer ids the service must expose. Kept here as an
// independent restatement of the registry contract so a drift in either
// direction (added or dropped observer) fails this test.
const std::set<std::string> kExpectedObserverIds{
    "white_snr",      "black_psnr", "burst_level",
    "closed_caption", "biphase",    "colour_frame_phase",
    "disc_quality",   "fm_code",    "white_flag"};

TEST(CoreObservationService, AvailableObservers_ExposesEveryStandardObserver) {
  CoreObservationService service;
  const auto infos = service.available_observers();

  ASSERT_EQ(infos.size(), kExpectedObserverIds.size());

  std::set<std::string> seen_ids;
  for (const auto& info : infos) {
    EXPECT_FALSE(info.id.empty());
    EXPECT_FALSE(info.version.empty()) << "id=" << info.id;
    EXPECT_FALSE(info.provided_observations.empty()) << "id=" << info.id;
    EXPECT_TRUE(seen_ids.insert(info.id).second) << "duplicate id: " << info.id;
  }

  EXPECT_EQ(seen_ids, kExpectedObserverIds);
}

TEST(CoreObservationService, CreateObserver_ReturnsHandle_ForEveryKnownId) {
  CoreObservationService service;
  for (const auto& id : kExpectedObserverIds) {
    auto handle = service.create_observer(id);
    EXPECT_NE(handle, nullptr) << "id=" << id;
  }
}

TEST(CoreObservationService, CreateObserver_ReturnsNull_ForUnknownId) {
  CoreObservationService service;
  EXPECT_EQ(service.create_observer("does_not_exist"), nullptr);
  EXPECT_EQ(service.create_observer(""), nullptr);
}

TEST(CoreObservationService, RunObserver_ReturnsFalse_ForUnknownId) {
  CoreObservationService service;
  NiceMock<orc_unit_test::MockVideoFrameRepresentation> vfr;
  // A strict context: an unknown id must not touch the context at all.
  ::testing::StrictMock<orc_unit_test::MockObservationContext> context;

  EXPECT_FALSE(
      service.run_observer("does_not_exist", vfr, FrameID(0), context));
}

TEST(CoreObservationService, RunObserver_DeliversResults_ThroughContext) {
  CoreObservationService service;

  // FieldQualityObserver writes zeroed disc_quality metrics for both derived
  // fields when the frame carries no video parameters — a deterministic path
  // that needs no real sample data.
  NiceMock<orc_unit_test::MockVideoFrameRepresentation> vfr;
  ON_CALL(vfr, get_video_parameters())
      .WillByDefault(Return(std::optional<SourceParameters>{}));

  orc_unit_test::MockObservationContext context;
  // Two derived fields (frame 0 -> field ids 0 and 1), three keys each.
  EXPECT_CALL(context, set(_, "disc_quality", "quality_score", _)).Times(2);
  EXPECT_CALL(context, set(_, "disc_quality", "dropout_count", _)).Times(2);
  EXPECT_CALL(context, set(_, "disc_quality", "phase_valid", _)).Times(2);

  EXPECT_TRUE(service.run_observer("disc_quality", vfr, FrameID(0), context));
}

TEST(CoreObservationService, CreateObserver_HandleForwardsToObserver) {
  CoreObservationService service;
  auto handle = service.create_observer("disc_quality");
  ASSERT_NE(handle, nullptr);

  NiceMock<orc_unit_test::MockVideoFrameRepresentation> vfr;
  ON_CALL(vfr, get_video_parameters())
      .WillByDefault(Return(std::optional<SourceParameters>{}));

  orc_unit_test::MockObservationContext context;
  EXPECT_CALL(context, set(_, "disc_quality", _, _)).Times(6);

  handle->process_frame(vfr, FrameID(0), context);
}

}  // namespace
}  // namespace tests
}  // namespace orc
