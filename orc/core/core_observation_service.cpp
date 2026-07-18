/*
 * File:        core_observation_service.cpp
 * Module:      orc-core
 * Purpose:     Host implementation of the plugin-facing IObservationService
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "core_observation_service.h"

#include <biphase_observer.h>
#include <black_psnr_observer.h>
#include <burst_level_observer.h>
#include <closed_caption_observer.h>
#include <colour_frame_phase_observer.h>
#include <field_quality_observer.h>
#include <fm_code_observer.h>
#include <observer.h>
#include <orc/stage/video_frame_representation.h>
#include <white_flag_observer.h>
#include <white_snr_observer.h>

#include <array>
#include <utility>

namespace orc {

namespace {

// Factory signature for a single standard observer.
using ObserverFactory = std::unique_ptr<Observer> (*)();

// Registry entry binding a stable id to its observer factory.
struct ObserverRegistryEntry {
  const char* id;
  ObserverFactory factory;
};

template <typename T>
std::unique_ptr<Observer> make_observer() {
  return std::make_unique<T>();
}

// Single source of truth for observer identity. The id strings are the stable
// public contract (they reuse each observer's primary observation namespace)
// and must never be renamed. The enumeration order fixes the order of
// available_observers().
constexpr std::array<ObserverRegistryEntry, 9> kObserverRegistry{{
    {"white_snr", &make_observer<WhiteSNRObserver>},
    {"black_psnr", &make_observer<BlackPSNRObserver>},
    {"burst_level", &make_observer<BurstLevelObserver>},
    {"closed_caption", &make_observer<ClosedCaptionObserver>},
    {"biphase", &make_observer<BiphaseObserver>},
    {"colour_frame_phase", &make_observer<ColourFramePhaseObserver>},
    {"disc_quality", &make_observer<FieldQualityObserver>},
    {"fm_code", &make_observer<FmCodeObserver>},
    {"white_flag", &make_observer<WhiteFlagObserver>},
}};

// Look up a factory by id. Returns nullptr for an unknown id (no throw).
ObserverFactory find_factory(const std::string& observer_id) {
  for (const auto& entry : kObserverRegistry) {
    if (observer_id == entry.id) {
      return entry.factory;
    }
  }
  return nullptr;
}

// Handle wrapping a host-allocated observer instance. Per-caller state (e.g.
// ClosedCaptionObserver's cross-field pairing) lives in the owned observer.
class CoreObserverHandle final : public IObserverHandle {
 public:
  explicit CoreObserverHandle(std::unique_ptr<Observer> observer)
      : observer_(std::move(observer)) {}

  void process_frame(const VideoFrameRepresentation& representation,
                     FrameID frame_id, IObservationContext& context) override {
    observer_->process_frame(representation, frame_id, context);
  }

 private:
  std::unique_ptr<Observer> observer_;
};

}  // namespace

std::vector<ObserverInfo> CoreObservationService::available_observers() const {
  std::vector<ObserverInfo> infos;
  infos.reserve(kObserverRegistry.size());
  for (const auto& entry : kObserverRegistry) {
    auto observer = entry.factory();
    infos.push_back(ObserverInfo{entry.id, observer->observer_version(),
                                 observer->get_provided_observations()});
  }
  return infos;
}

std::unique_ptr<IObserverHandle> CoreObservationService::create_observer(
    const std::string& observer_id) const {
  ObserverFactory factory = find_factory(observer_id);
  if (!factory) {
    return nullptr;
  }
  return std::make_unique<CoreObserverHandle>(factory());
}

bool CoreObservationService::run_observer(
    const std::string& observer_id,
    const VideoFrameRepresentation& representation, FrameID frame_id,
    IObservationContext& context) const {
  ObserverFactory factory = find_factory(observer_id);
  if (!factory) {
    return false;
  }
  auto observer = factory();
  observer->process_frame(representation, frame_id, context);
  return true;
}

}  // namespace orc
