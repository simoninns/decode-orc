/*
 * File:        logging.h
 * Module:      orc-common
 * Purpose:     Shared logging convenience header for GUI/CLI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <spdlog/spdlog.h>
#include <memory>
#include <string>

namespace orc {

/// Get the application logger (created on first use)
std::shared_ptr<spdlog::logger> get_app_logger();

/// Reset the application logger (it will be recreated on next use)
void reset_logging();

/// Initialize application logging independently of core
void init_app_logging(const std::string& level = "info",
                  const std::string& pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v",
                  const std::string& log_file = "",
                  const std::string& logger_name = "orc-app");

} // namespace orc

// Application-wide logging macros that use the app logger
#define ORC_LOG_TRACE(...)    SPDLOG_LOGGER_TRACE(orc::get_app_logger(), __VA_ARGS__)
#define ORC_LOG_DEBUG(...)    SPDLOG_LOGGER_DEBUG(orc::get_app_logger(), __VA_ARGS__)
#define ORC_LOG_INFO(...)     SPDLOG_LOGGER_INFO(orc::get_app_logger(), __VA_ARGS__)
#define ORC_LOG_WARN(...)     SPDLOG_LOGGER_WARN(orc::get_app_logger(), __VA_ARGS__)
#define ORC_LOG_ERROR(...)    SPDLOG_LOGGER_ERROR(orc::get_app_logger(), __VA_ARGS__)
#define ORC_LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(orc::get_app_logger(), __VA_ARGS__)
