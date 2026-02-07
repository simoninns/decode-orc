/*
 * File:        logging.cpp
 * Module:      orc-common
 * Purpose:     Shared logging implementation for GUI/CLI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "include/logging.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <mutex>

namespace orc {

static std::shared_ptr<spdlog::logger> g_logger;
static std::mutex g_logger_mutex;

std::shared_ptr<spdlog::logger> get_app_logger() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    if (!g_logger) {
        // Create a default console logger
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        g_logger = std::make_shared<spdlog::logger>("orc-app", sink);
        spdlog::register_logger(g_logger);
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        g_logger->set_level(spdlog::level::info);
    }
    return g_logger;
}

void reset_logging() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    if (g_logger) {
        spdlog::drop(g_logger->name());
        g_logger.reset();
    }
}

static spdlog::level::level_enum parse_level(const std::string& level) {
    std::string l = level;
    for (auto& c : l) c = static_cast<char>(::tolower(c));
    if (l == "trace") return spdlog::level::trace;
    if (l == "debug") return spdlog::level::debug;
    if (l == "info") return spdlog::level::info;
    if (l == "warn" || l == "warning") return spdlog::level::warn;
    if (l == "error") return spdlog::level::err;
    if (l == "critical") return spdlog::level::critical;
    if (l == "off") return spdlog::level::off;
    return spdlog::level::info;
}

void init_app_logging(const std::string& level,
                  const std::string& pattern,
                  const std::string& log_file,
                  const std::string& logger_name) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    
    // Drop existing logger if present
    if (g_logger) {
        spdlog::drop(g_logger->name());
        g_logger.reset();
    }
    
    // Create new logger with proper name
    auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    g_logger = std::make_shared<spdlog::logger>(logger_name, sink);
    spdlog::register_logger(g_logger);
    g_logger->set_pattern(pattern);
    g_logger->set_level(parse_level(level));

    if (!log_file.empty()) {
        try {
            // Add file sink while keeping console output
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
            file_sink->set_pattern(pattern);
            g_logger->sinks().push_back(file_sink);
        } catch (...) {
            // If file sink creation fails, keep console logging only
        }
    }
}

} // namespace orc
