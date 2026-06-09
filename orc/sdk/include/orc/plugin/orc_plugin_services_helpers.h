/*
 * File:        orc_plugin_services_helpers.h
 * Module:      decode-orc Plugin SDK
 * Purpose:     Helper macros for using OrcPluginServices.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 *
 * STABILITY: PUBLIC — This header is part of the stable plugin SDK.
 *
 * NOTE: This file must be included AFTER complete type definitions are
 * available (especially when using the macros that might format using preview
 * types).
 */

#pragma once

#include <orc/plugin/orc_plugin_services.h>

// =============================================================================
// Logging macros
// =============================================================================
//
// These macros are the plugin-side counterparts of the ORC_LOG_* host macros.
// They format a message and forward it to the host logging backend via the
// services table.
//
// Usage:
//   ORC_PLUGIN_LOG_DEBUG("Processing frame {}", frame_index);
//   ORC_PLUGIN_LOG_INFO("Plugin started");
//   ORC_PLUGIN_LOG_WARN("Unexpected state: {}", state);
//   ORC_PLUGIN_LOG_ERROR("Failed to decode frame {}", frame_index);
//
// Requirements:
//   A <fmt/format.h>-compatible implementation must be visible (bundled with
//   spdlog).  If fmt is not available the macros expand to no-ops.

#if defined(FMT_VERSION) || __has_include(<fmt/format.h>)

#define ORC_PLUGIN_LOG_IMPL_(level_, fmt_str_, ...)                    \
  do {                                                                 \
    if (::orc::plugin::g_services && ::orc::plugin::g_services->log) { \
      std::string _msg_ = fmt::format(fmt_str_, ##__VA_ARGS__);        \
      ::orc::plugin::g_services->log(level_, _msg_.c_str());           \
    }                                                                  \
  } while (0)

#define ORC_PLUGIN_LOG_TRACE(fmt_str_, ...) \
  ORC_PLUGIN_LOG_IMPL_(::orc::OrcPluginLogLevel::Trace, fmt_str_, ##__VA_ARGS__)
#define ORC_PLUGIN_LOG_DEBUG(fmt_str_, ...) \
  ORC_PLUGIN_LOG_IMPL_(::orc::OrcPluginLogLevel::Debug, fmt_str_, ##__VA_ARGS__)
#define ORC_PLUGIN_LOG_INFO(fmt_str_, ...) \
  ORC_PLUGIN_LOG_IMPL_(::orc::OrcPluginLogLevel::Info, fmt_str_, ##__VA_ARGS__)
#define ORC_PLUGIN_LOG_WARN(fmt_str_, ...) \
  ORC_PLUGIN_LOG_IMPL_(::orc::OrcPluginLogLevel::Warn, fmt_str_, ##__VA_ARGS__)
#define ORC_PLUGIN_LOG_ERROR(fmt_str_, ...) \
  ORC_PLUGIN_LOG_IMPL_(::orc::OrcPluginLogLevel::Error, fmt_str_, ##__VA_ARGS__)
#define ORC_PLUGIN_LOG_CRITICAL(fmt_str_, ...)                       \
  ORC_PLUGIN_LOG_IMPL_(::orc::OrcPluginLogLevel::Critical, fmt_str_, \
                       ##__VA_ARGS__)

#else  // fmt not available — no-op stubs

#define ORC_PLUGIN_LOG_TRACE(...) \
  do {                            \
  } while (0)
#define ORC_PLUGIN_LOG_DEBUG(...) \
  do {                            \
  } while (0)
#define ORC_PLUGIN_LOG_INFO(...) \
  do {                           \
  } while (0)
#define ORC_PLUGIN_LOG_WARN(...) \
  do {                           \
  } while (0)
#define ORC_PLUGIN_LOG_ERROR(...) \
  do {                            \
  } while (0)
#define ORC_PLUGIN_LOG_CRITICAL(...) \
  do {                               \
  } while (0)

#endif  // FMT_VERSION
