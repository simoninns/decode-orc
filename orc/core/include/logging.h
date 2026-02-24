/*
 * File:        logging.h
 * Module:      orc-core
 * Purpose:     Logging system implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>

namespace orc {

/// Initialize the logging system
/// Should be called once at application startup
/// @param level Log level (trace, debug, info, warn, error, critical, off)
/// @param pattern Optional custom pattern (default: "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v")
/// @param log_file Optional file path to write logs to (in addition to console)
void init_logging(const std::string& level = "info", 
                  const std::string& pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v",
                  const std::string& log_file = "");

/// Get the default logger
std::shared_ptr<spdlog::logger> get_logger();

/// Set log level at runtime
void set_log_level(const std::string& level);

} // namespace orc

// Convenient logging macros
#define ORC_LOG_TRACE(...)    SPDLOG_LOGGER_TRACE(orc::get_logger(), __VA_ARGS__)
#define ORC_LOG_DEBUG(...)    SPDLOG_LOGGER_DEBUG(orc::get_logger(), __VA_ARGS__)
#define ORC_LOG_INFO(...)     SPDLOG_LOGGER_INFO(orc::get_logger(), __VA_ARGS__)
#define ORC_LOG_WARN(...)     SPDLOG_LOGGER_WARN(orc::get_logger(), __VA_ARGS__)
#define ORC_LOG_ERROR(...)    SPDLOG_LOGGER_ERROR(orc::get_logger(), __VA_ARGS__)
#define ORC_LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(orc::get_logger(), __VA_ARGS__)

#define LOG_TRACE(...)    ORC_LOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...)    ORC_LOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)     ORC_LOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)     ORC_LOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...)    ORC_LOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) ORC_LOG_CRITICAL(__VA_ARGS__)
