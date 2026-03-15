/*
 * File:        logging.h
 * Module:      orc-metadata
 * Purpose:     Logging wrapper for orc-metadata (spdlog default logger, no orc-core dependency)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <spdlog/spdlog.h>

// Convenience macros matching the ORC_LOG_* API used throughout the codebase.
// These forward to spdlog's default logger so that orc-metadata has no link-time
// dependency on orc-core's orc::get_logger() function.
#define ORC_LOG_TRACE(...)    SPDLOG_TRACE(__VA_ARGS__)
#define ORC_LOG_DEBUG(...)    SPDLOG_DEBUG(__VA_ARGS__)
#define ORC_LOG_INFO(...)     SPDLOG_INFO(__VA_ARGS__)
#define ORC_LOG_WARN(...)     SPDLOG_WARN(__VA_ARGS__)
#define ORC_LOG_ERROR(...)    SPDLOG_ERROR(__VA_ARGS__)
