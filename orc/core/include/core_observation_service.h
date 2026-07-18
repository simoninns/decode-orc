/*
 * File:        core_observation_service.h
 * Module:      orc-core
 * Purpose:     Host implementation of the plugin-facing IObservationService
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <orc/stage/frame_id.h>
#include <orc/stage/observation/observation_context_interface.h>
#include <orc/stage/observation/observation_service_interface.h>

#include <memory>
#include <string>
#include <vector>

namespace orc {

class VideoFrameRepresentation;

/**
 * @brief Host-owned observation service backed by the standard observer set.
 *
 * The registry mapping observer id -> concrete observer factory is the single
 * source of truth for observer identity (see core_observation_service.cpp). The
 * class is stateless: it owns no per-run data, so a single instance can back
 * every plugin. Unknown ids fail cleanly — create_observer() returns nullptr
 * and run_observer() returns false; nothing throws across the plugin boundary.
 *
 * Thread-safety: all methods are const and reentrant; the object may be shared
 * across threads. Per-handle threading rules are documented on IObserverHandle.
 */
class CoreObservationService final : public IObservationService {
 public:
  std::vector<ObserverInfo> available_observers() const override;

  std::unique_ptr<IObserverHandle> create_observer(
      const std::string& observer_id) const override;

  bool run_observer(const std::string& observer_id,
                    const VideoFrameRepresentation& representation,
                    FrameID frame_id,
                    IObservationContext& context) const override;
};

}  // namespace orc
