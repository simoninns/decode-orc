/*
 * File:        orc_plugin_sdk.h
 * Module:      decode-orc Plugin SDK
 * Purpose:     Umbrella include for the decode-orc stage plugin SDK
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 *
 * STABILITY: PUBLIC — This is the recommended single include point for plugin
 *            implementors. All types needed to implement, register, and export
 *            a decode-orc stage plugin are available after including this file.
 *
 * QUICK START:
 *
 *   #include <orc/plugin/orc_plugin_sdk.h>
 *   #include "my_stage_implementation.h"   // Your DAGStage subclass
 *
 *   namespace {
 *   orc::DAGStagePtr create_my_stage() {
 *       return std::make_shared<MyStage>();
 *   }
 *   constexpr orc::StagePluginDescriptor kDescriptor{
 *       "com.example.stage.my_filter",
 *       "1.0.0",
 *       orc::kStagePluginHostAbiVersion,
 *       orc::kStagePluginApiVersion,
 *       "MIT",
 *       false,
 *   };
 *   } // namespace
 *
 *   ORC_STAGE_PLUGIN_EXPORT const orc::StagePluginDescriptor*
 *   orc_get_stage_plugin_descriptor() {
 *       return &kDescriptor;
 *   }
 *
 *   ORC_STAGE_PLUGIN_EXPORT bool orc_register_stage_plugin(
 *       const orc::OrcPluginServices* services, void* ctx,
 *       bool (*reg)(void*, const char*, orc::OrcStageFactoryFn),
 *       const char** err)
 *   {
 *       orc::plugin::set_services(services);
 *       if (!reg) { if (err) *err = "null callback"; return false; }
 *       if (!reg(ctx, "my_filter", &create_my_stage)) {
 *           if (err) *err = "registration failed"; return false;
 *       }
 *       return true;
 *   }
 *
 * CMAKE INTEGRATION:
 *   In your plugin's CMakeLists.txt:
 *
 *     find_package(decode-orc-plugin-sdk REQUIRED)
 *     orc_add_stage_plugin(
 *         my-stage-plugin
 *         OUTPUT_NAME my-stage-plugin
 *         PLUGIN_VERSION "1.0.0"
 *         SOURCES plugin.cpp my_stage_implementation.cpp
 *     )
 *
 *   Or for in-tree builds, link directly:
 *
 *     target_link_libraries(my-stage-plugin PRIVATE orc-plugin-sdk)
 */

#pragma once

// Host ABI contract: entrypoints, descriptor, versioning, export macro.
#include <orc/plugin/orc_plugin_abi.h>

// Stage API: parameter types, node metadata, ParameterizedStage,
// TriggerableStage.
#include <orc/plugin/orc_stage_api.h>

// Stage runtime helpers for plugin stage implementations.
#include <orc/plugin/orc_stage_runtime.h>

// Preview capability and carrier contracts for preview-capable stages.
#include <orc/plugin/orc_stage_preview.h>

// SDK Stage API includes key stage infrastructure: ParameterizedStage,
// TriggerableStage, and preview interfaces. Consolidated stage services API:
// canonical host services for sink/file output and related stage runtime
// helpers.
#include <orc/plugin/orc_stage_services.h>

// Canonical stage helper/tooling descriptors used by GUI/CLI integrations.
#include <orc/plugin/orc_stage_tooling.h>

// Host service table: OrcPluginServices, OrcPluginLogLevel.
// (Must come after orc_stage_api.h so that forward declarations of preview
// types work.)
#include <orc/plugin/orc_plugin_services.h>

// Plugin service helpers: ORC_PLUGIN_LOG_* logging macros.
// (Must come after all other includes so that preview types are fully defined
// when inline functions are instantiated.)
#include <orc/plugin/orc_plugin_services_helpers.h>
