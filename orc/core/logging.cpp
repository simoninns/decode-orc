/*
 * File:        logging.cpp
 * Module:      orc-core
 * Purpose:     Logging system implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "logging.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace orc {

static std::shared_ptr<spdlog::logger> g_logger;

void init_logging(const std::string& level, const std::string& pattern) {
    if (!g_logger) {
        // Create console logger with color
        g_logger = spdlog::stdout_color_mt("orc");
        g_logger->set_pattern(pattern);
    }
    
    // Set log level
    set_log_level(level);
}

std::shared_ptr<spdlog::logger> get_logger() {
    if (!g_logger) {
        // Auto-initialize if not done yet
        init_logging();
    }
    return g_logger;
}

void set_log_level(const std::string& level) {
    auto logger = get_logger();
    
    if (level == "trace") {
        logger->set_level(spdlog::level::trace);
    } else if (level == "debug") {
        logger->set_level(spdlog::level::debug);
    } else if (level == "info") {
        logger->set_level(spdlog::level::info);
    } else if (level == "warn" || level == "warning") {
        logger->set_level(spdlog::level::warn);
    } else if (level == "error") {
        logger->set_level(spdlog::level::err);
    } else if (level == "critical") {
        logger->set_level(spdlog::level::critical);
    } else if (level == "off") {
        logger->set_level(spdlog::level::off);
    } else {
        logger->warn("Unknown log level '{}', using 'info'", level.c_str());
        logger->set_level(spdlog::level::info);
    }
}

} // namespace orc
