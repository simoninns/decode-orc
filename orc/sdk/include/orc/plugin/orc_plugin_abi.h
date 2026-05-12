/*
 * File:        orc_plugin_abi.h
 * Module:      decode-orc Plugin SDK
 * Purpose:     Host ABI contract for decode-orc stage plugins
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 *
 * STABILITY: PUBLIC — This header is part of the stable plugin SDK.
 *            Breaking changes to any type or constant defined here require a
 *            host_abi_version bump and a deprecation window.
 *
 * USAGE:
 *   Plugin implementors should include <orc/plugin/orc_plugin_sdk.h> (the
 *   umbrella header) rather than including this file directly.
 *
 * VERSIONING:
 *   host_abi_version — binary compatibility boundary.
 *     Bumped when StagePluginDescriptor layout, entrypoint signatures, or the
 *     fundamental callback contract change in a binary-incompatible way.
 *     Plugins encode the host_abi_version they were compiled against.
 *     The host rejects plugins with a mismatched host_abi_version.
 *
 *   plugin_api_version — stage contract compatibility boundary.
 *     Bumped when the DAGStage interface, parameter schema, observation
 *     contract, or lifecycle semantics change in an incompatible way.
 *     Plugins encode the plugin_api_version they were compiled against.
 *     The host rejects plugins with a mismatched plugin_api_version.
 */

#pragma once

#include <cstdint>
#include <memory>

namespace orc {

// =============================================================================
// Forward declarations
// =============================================================================

/// Forward declaration of DAGStage. Full definition is in orc_stage_api.h and
/// in the stage-specific headers included by plugin implementations.
class DAGStage;

/// Forward declaration of the host service table.  Full definition is in
/// orc_plugin_services.h (included by the orc_plugin_sdk.h umbrella).
struct OrcPluginServices;

// =============================================================================
// Version constants
// =============================================================================

/// Host ABI version — binary compatibility boundary.
///
/// History:
///   1 — Initial release (StagePluginDescriptor without plugin_api_version).
///   2 — Added plugin_api_version field to StagePluginDescriptor (Phase 4).
///   3 — Added OrcPluginServices table; orc_register_stage_plugin now receives
///        a const OrcPluginServices* as its first parameter. Plugins must use
///        the services table for logging instead of resolving host symbols
///        directly.
inline constexpr uint32_t kStagePluginHostAbiVersion = 3;

/// Preprocessor alias for kStagePluginHostAbiVersion.  Allows plugin code to
/// use conditional compilation:
///   #if ORC_SDK_ABI_VERSION >= 3
///     // use OrcPluginServices
///   #endif
#define ORC_SDK_ABI_VERSION 3

/// Plugin API version — stage contract compatibility boundary.
///
/// History:
///   1 — Initial public API surface (Phase 4).
inline constexpr uint32_t kStagePluginApiVersion = 1;

// =============================================================================
// Plugin entrypoint symbol names
// =============================================================================

/// Symbol name of the descriptor query entrypoint exported by every plugin.
inline constexpr const char* kGetStagePluginDescriptorSymbol = "orc_get_stage_plugin_descriptor";

/// Symbol name of the stage registration entrypoint exported by every plugin.
inline constexpr const char* kRegisterStagePluginSymbol = "orc_register_stage_plugin";

// =============================================================================
// Plugin descriptor
// =============================================================================

/// Plugin descriptor returned by orc_get_stage_plugin_descriptor().
///
/// All pointer fields must point to static storage valid for the lifetime of
/// the loaded plugin binary.
///
/// ABI note: fields must not be reordered. New fields are always appended.
/// Any layout change requires a host_abi_version bump.
struct StagePluginDescriptor {
    const char* plugin_id;       ///< Reverse-domain unique ID, e.g. "com.example.stage.my_filter"
    const char* plugin_version;  ///< Semantic version string, e.g. "1.2.3"
    uint32_t host_abi_version;   ///< Must equal orc::kStagePluginHostAbiVersion at load time
    uint32_t plugin_api_version; ///< Must equal orc::kStagePluginApiVersion at load time
    const char* license_spdx;    ///< SPDX license expression, e.g. "GPL-3.0-or-later"
    bool is_core_plugin;         ///< true only for stages bundled with the Decode-Orc distribution
};

// =============================================================================
// Function pointer types
// =============================================================================

/// Factory function that allocates and returns a new stage instance.
/// The host calls this to create stage objects on demand.
using OrcStageFactoryFn = std::shared_ptr<DAGStage>(*)();

/// Signature of the orc_get_stage_plugin_descriptor() entrypoint.
using OrcGetStagePluginDescriptorFn = const StagePluginDescriptor*(*)();

/// Signature of the orc_register_stage_plugin() entrypoint.
///
/// @param services      Host-supplied service table.  Plugins must store this
///                      pointer before calling register_stage; the pointer
///                      remains valid for the lifetime of the plugin.
///                      Plugins should check services->services_size before
///                      accessing fields beyond what their compiled ABI version
///                      guarantees.
/// @param context       Opaque context pointer provided by the host; must be
///                      passed unchanged to the register_stage callback.
/// @param register_stage Callback the plugin calls once per stage it exports.
///                      Returns false if the host rejects the registration.
/// @param error_message Optional: plugin may set this to a static error string
///                      on failure. Must point to static storage.
/// @return true if all stages were registered successfully; false otherwise.
using OrcRegisterStagePluginFn = bool(*)(
    const OrcPluginServices* services,
    void* context,
    bool(*register_stage)(void* context, const char* stage_name, OrcStageFactoryFn factory),
    const char** error_message);

// =============================================================================
// Platform export macro
// =============================================================================

/// Apply this macro to both required plugin entrypoints so the linker exports
/// them with C linkage and default visibility.
///
/// Example:
///   ORC_STAGE_PLUGIN_EXPORT const orc::StagePluginDescriptor* orc_get_stage_plugin_descriptor() { ... }
///   ORC_STAGE_PLUGIN_EXPORT bool orc_register_stage_plugin(void*, ...) { ... }
#if defined(_WIN32)
#define ORC_STAGE_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define ORC_STAGE_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

} // namespace orc
