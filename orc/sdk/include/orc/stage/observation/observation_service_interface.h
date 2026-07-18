/*
 * File:        observation_service_interface.h
 * Module:      decode-orc Plugin SDK (stage contract)
 * Purpose:     Host-owned observation service reached across the plugin
 * boundary
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

// SDK TIER: stage/observation — stage contract type crossing the plugin
// boundary. A layout change here bumps the host ABI version.

#include <orc/stage/frame_id.h>
#include <orc/stage/observation/observation_context_interface.h>
#include <orc/stage/observation/observation_schema.h>

#include <memory>
#include <string>
#include <vector>

namespace orc {

// Forward declaration. Only a reference to a VideoFrameRepresentation crosses
// this interface, so the full definition
// (orc/stage/video_frame_representation.h) is not required by callers that
// merely hold the service pointer.
class VideoFrameRepresentation;

/**
 * @brief Static description of a single observer offered by the service.
 *
 * The @ref id is the stable string identifier callers pass to
 * @ref IObservationService::create_observer / run_observer. It reuses the
 * observation namespace an observer is primarily associated with (e.g.
 * "white_snr", "biphase", "disc_quality") and never changes once published.
 */
struct ObserverInfo {
  std::string id;       ///< Stable observer identifier (never renamed).
  std::string version;  ///< Observer version string (e.g. "1.0.0").
  std::vector<ObservationKey>
      provided_observations;  ///< Keys the observer writes.
};

/**
 * @brief Host-owned per-caller observer session.
 *
 * A handle owns the mutable state a single logical observation stream needs
 * (e.g. ClosedCaptionObserver pairs successive fields across process_frame
 * calls). Each caller that requires cross-frame continuity holds its own
 * handle; state therefore lives in a host-allocated instance rather than in
 * any global.
 *
 * Thread-safety: a single handle is NOT thread-safe. All process_frame() calls
 * on one handle must be serialised by the caller (they typically run on one
 * pipeline worker). Distinct handles are independent and may be driven from
 * different threads concurrently.
 *
 * Ownership: returned by value as std::unique_ptr from
 * IObservationService::create_observer(); the handle must not outlive the
 * IObservationService that produced it.
 */
class IObserverHandle {
 public:
  virtual ~IObserverHandle() = default;

  /**
   * @brief Process a single frame, writing observations into @p context.
   *
   * Semantics match Observer::process_frame(): the observer iterates both
   * fields of @p frame_id and writes namespaced, per-field observations.
   * Successive calls on the same handle may accumulate cross-frame state.
   *
   * @param representation Video frame representation (CVBS_U10_4FSC domain).
   * @param frame_id       Frame identifier.
   * @param context        Caller-supplied context to populate. The handle does
   *                       not retain a reference to it beyond the call.
   */
  virtual void process_frame(const VideoFrameRepresentation& representation,
                             FrameID frame_id,
                             IObservationContext& context) = 0;
};

/**
 * @brief Host service granting plugins access to the standard observers.
 *
 * Observer implementations live once, in the host; plugins select them by
 * stable string id rather than by linking the concrete observer classes. The
 * set of available observers is fixed by the host at build time (see
 * available_observers()); the service exposes no configuration surface — no
 * standard observer is configurable. Should a configurable observer ever be
 * required, a configuration-taking overload will be appended to this interface
 * (append-only vtable growth, per orc_plugin_abi.h), never inserted.
 *
 * Thread-safety: available_observers() and create_observer() are const and
 * thread-safe; they may be called concurrently from any thread. run_observer()
 * constructs a throwaway handle per call and is likewise thread-safe with
 * respect to the service, but the caller must still ensure the supplied
 * @p context is not being mutated concurrently. Per-handle state has the
 * threading rules documented on IObserverHandle.
 *
 * Boundary safety: no method throws across the plugin boundary. An unknown
 * observer id yields an empty result (null handle / false), never an
 * exception.
 */
class IObservationService {
 public:
  virtual ~IObservationService() = default;

  /**
   * @brief Enumerate every observer the service can create.
   *
   * @return One ObserverInfo per available observer, in a stable order.
   */
  virtual std::vector<ObserverInfo> available_observers() const = 0;

  /**
   * @brief Create a stateful observer session for @p observer_id.
   *
   * Use this when observations must accumulate across frames (or simply to
   * amortise per-observer setup across a run).
   *
   * @param observer_id Stable id from available_observers().
   * @return An owning handle, or nullptr if @p observer_id is unknown.
   */
  virtual std::unique_ptr<IObserverHandle> create_observer(
      const std::string& observer_id) const = 0;

  /**
   * @brief One-shot convenience wrapper over create_observer().
   *
   * Creates a fresh handle, processes exactly one frame, and discards the
   * handle. Equivalent to create_observer(observer_id)->process_frame(...) for
   * stateless use; do not use it where cross-frame state matters.
   *
   * @param observer_id    Stable id from available_observers().
   * @param representation Video frame representation (CVBS_U10_4FSC domain).
   * @param frame_id       Frame identifier.
   * @param context        Caller-supplied context to populate.
   * @return true if @p observer_id was recognised and the frame processed;
   *         false if @p observer_id is unknown (context left untouched).
   */
  virtual bool run_observer(const std::string& observer_id,
                            const VideoFrameRepresentation& representation,
                            FrameID frame_id,
                            IObservationContext& context) const = 0;
};

}  // namespace orc
