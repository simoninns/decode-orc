/*
 * File:        logging.h
 * Module:      orc-gui
 * Purpose:     GUI logging convenience header
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "../core/include/logging.h"
#include <spdlog/spdlog.h>

namespace orc {

/// Get the GUI-specific logger
std::shared_ptr<spdlog::logger> get_gui_logger();

} // namespace orc

// GUI-specific logging macros that use the GUI logger
#undef ORC_LOG_TRACE
#undef ORC_LOG_DEBUG
#undef ORC_LOG_INFO
#undef ORC_LOG_WARN
#undef ORC_LOG_ERROR
#undef ORC_LOG_CRITICAL

#define ORC_LOG_TRACE(...)    SPDLOG_LOGGER_TRACE(orc::get_gui_logger(), __VA_ARGS__)
#define ORC_LOG_DEBUG(...)    SPDLOG_LOGGER_DEBUG(orc::get_gui_logger(), __VA_ARGS__)
#define ORC_LOG_INFO(...)     SPDLOG_LOGGER_INFO(orc::get_gui_logger(), __VA_ARGS__)
#define ORC_LOG_WARN(...)     SPDLOG_LOGGER_WARN(orc::get_gui_logger(), __VA_ARGS__)
#define ORC_LOG_ERROR(...)    SPDLOG_LOGGER_ERROR(orc::get_gui_logger(), __VA_ARGS__)
#define ORC_LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(orc::get_gui_logger(), __VA_ARGS__)
