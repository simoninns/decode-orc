/*
 * File:        observation_service_accessor_test.cpp
 * Module:      orc-tests/core/unit/contracts
 * Purpose:     ABI guard test for plugin::get_observation_service()
 *
 * Verifies the services_size guard on the ABI 9 observation_service field: an
 * older host (smaller services_size) or an unset services pointer yields
 * nullptr rather than reading past the struct the host actually populated.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>
#include <orc/abi/orc_plugin_services.h>
#include <orc/stage/observation/observation_service_interface.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace orc {
namespace tests {
namespace {

// Minimal stand-in so a non-null service pointer can be handed to the guard.
class StubObservationService : public IObservationService {
 public:
  std::vector<ObserverInfo> available_observers() const override { return {}; }
  std::unique_ptr<IObserverHandle> create_observer(
      const std::string&) const override {
    return nullptr;
  }
  bool run_observer(const std::string&, const VideoFrameRepresentation&,
                    FrameID, IObservationContext&) const override {
    return false;
  }
};

// Restore the module-level services pointer after each case so tests do not
// leak state into one another.
class ObservationServiceAccessorTest : public ::testing::Test {
 protected:
  void TearDown() override { plugin::set_services(nullptr); }
};

TEST_F(ObservationServiceAccessorTest, ReturnsNull_WhenServicesUnset) {
  plugin::set_services(nullptr);
  EXPECT_EQ(plugin::get_observation_service(), nullptr);
}

TEST_F(ObservationServiceAccessorTest, ReturnsNull_ForOlderHostServicesSize) {
  StubObservationService stub;
  OrcPluginServices services{};
  services.observation_service = &stub;
  // Simulate an ABI 8 host: services_size stops short of the appended field.
  services.services_size =
      static_cast<uint32_t>(offsetof(OrcPluginServices, observation_service));

  plugin::set_services(&services);
  EXPECT_EQ(plugin::get_observation_service(), nullptr);
}

TEST_F(ObservationServiceAccessorTest, ReturnsPointer_ForCurrentServicesSize) {
  StubObservationService stub;
  OrcPluginServices services{};
  services.observation_service = &stub;
  services.services_size = static_cast<uint32_t>(sizeof(OrcPluginServices));

  plugin::set_services(&services);
  EXPECT_EQ(plugin::get_observation_service(), &stub);
}

}  // namespace
}  // namespace tests
}  // namespace orc
