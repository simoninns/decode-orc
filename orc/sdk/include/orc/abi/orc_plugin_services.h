/*
 * File:        orc_plugin_services.h
 * Module:      decode-orc Plugin SDK
 * Purpose:     Host service table injected into plugins at registration time.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 *
 * STABILITY: PUBLIC — This header is part of the stable plugin SDK.
 *
 * USAGE:
 *   Include via the umbrella header <orc/abi/orc_plugin_sdk.h>.
 *   Do not include this file directly.
 *
 *   In your plugin's plugin.cpp, store the services pointer from the
 *   orc_register_stage_plugin() first parameter:
 *
 *     ORC_STAGE_PLUGIN_EXPORT bool orc_register_stage_plugin(
 *         const orc::OrcPluginServices* services, ...)
 *     {
 *         orc::plugin::set_services(services);
 *         ...
 *     }
 *
 *   Then use ORC_PLUGIN_LOG_DEBUG / INFO / WARN / ERROR macros in any
 *   translation unit.  The macros call the host-supplied logging callback
 *   through the stored services pointer.
 *
 *   For preview rendering, call the render_colour_preview function pointer
 *   on the stored services table (orc::plugin::g_services).
 *
 * ABI SAFETY:
 *   OrcPluginServices::services_size is set to sizeof(OrcPluginServices) by
 *   the host at the time it was compiled.  Plugins MUST check this field
 *   before accessing any field beyond the first (services_size itself).
 *   New fields are only ever appended, never reordered.
 */

#pragma once

// SDK TIER: abi — frozen binary contract (descriptor/entrypoints/registration/
// services). Any change to this header bumps the host ABI version.

#include <cstddef>
#include <cstdint>

// Forward declarations for preview types. Full definitions come from
// orc_rendering.h (included by orc_stage_api.h in the SDK umbrella).
namespace orc {
struct PreviewImage;
struct ColourFrameCarrier;
class IStageServices;
class IObservationService;

// =============================================================================
// Log level enum
// =============================================================================

/// Log severity levels for the OrcPluginServices.log callback.
/// Values intentionally match spdlog::level::level_enum.
enum class OrcPluginLogLevel : int {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warn = 3,
  Error = 4,
  Critical = 5,
};

// =============================================================================
// Service table
// =============================================================================

/// Service function pointers injected by the host into
/// orc_register_stage_plugin().
///
/// ABI rule: fields are append-only.  Plugins must check services_size before
/// accessing any field that was not present in the ABI version the plugin was
/// compiled against.
struct OrcPluginServices {
  /// sizeof(OrcPluginServices) at host build time.  Plugins use this to
  /// guard access to fields introduced in later ABI revisions.
  uint32_t services_size;

  // -------------------------------------------------------------------------
  // v3 fields (ABI version 3)
  // -------------------------------------------------------------------------

  /// Log a pre-formatted message at the given severity level.
  /// @param level    Severity.
  /// @param message  Null-terminated UTF-8 message string.
  ///                 Must not be nullptr.  Ownership is not transferred.
  void (*log)(OrcPluginLogLevel level, const char* message);

  /// Convert a decoded ColourFrameCarrier to a display-ready PreviewImage.
  ///
  /// Equivalent to the host-internal render_preview_from_colour_carrier().
  /// @param carrier  Pointer to the carrier to convert.  Must not be nullptr.
  ///                 The carrier's is_valid() must return true.
  /// @return A filled PreviewImage on success; an empty PreviewImage on error
  ///         (check width == 0 && height == 0).
  PreviewImage (*render_colour_preview)(const ColourFrameCarrier* carrier);

  // -------------------------------------------------------------------------
  // v3 extension fields (append-only; guarded by services_size)
  // -------------------------------------------------------------------------

  /// Optional consolidated stage services interface.
  ///
  /// When present, plugins should prefer this interface over any private host
  /// include-path workarounds for sink/file service requirements.
  ///
  /// Host may set this to nullptr when the capability is not available.
  IStageServices* stage_services;

  // -------------------------------------------------------------------------
  // v9 fields (ABI version 9; append-only, guarded by services_size)
  // -------------------------------------------------------------------------

  /// Host-owned observation service. Plugins request the standard observers by
  /// stable string id rather than linking the concrete observer classes; see
  /// <orc/stage/observation/observation_service_interface.h>.
  ///
  /// Host may set this to nullptr when the capability is not available. Older
  /// hosts (services_size below the offset of this field) never populate it,
  /// so plugins must reach it via plugin::get_observation_service().
  IObservationService* observation_service;
};

// =============================================================================
// Module-level services pointer (plugin-side storage)
// =============================================================================

namespace plugin {

/// Module-level services pointer.  Initialised by set_services() inside
/// orc_register_stage_plugin() before any stage factory is called.
///
/// Defined as inline so the same instance is shared across all translation
/// units within a single plugin shared library (C++17 inline variable rules).
inline const OrcPluginServices* g_services{nullptr};

/// Called by plugin.cpp inside orc_register_stage_plugin() to store the host-
/// supplied services table.  Must be called before any stage factory is
/// invoked.
inline void set_services(const OrcPluginServices* services) {
  g_services = services;
}

inline IStageServices* get_stage_services() {
  if (!g_services) {
    return nullptr;
  }

  const auto required_size = static_cast<uint32_t>(
      offsetof(OrcPluginServices, stage_services) + sizeof(IStageServices*));
  if (g_services->services_size < required_size) {
    return nullptr;
  }

  return g_services->stage_services;
}

inline IObservationService* get_observation_service() {
  if (!g_services) {
    return nullptr;
  }

  const auto required_size =
      static_cast<uint32_t>(offsetof(OrcPluginServices, observation_service) +
                            sizeof(IObservationService*));
  if (g_services->services_size < required_size) {
    return nullptr;
  }

  return g_services->observation_service;
}

}  // namespace plugin
}  // namespace orc
