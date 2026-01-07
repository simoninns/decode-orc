/*
 * File:        logging.cpp
 * Module:      orc-core
 * Purpose:     Logging system implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "logging.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <vector>

namespace orc {

static std::shared_ptr<spdlog::logger> g_logger;

void init_logging(const std::string& level, const std::string& pattern, const std::string& log_file) {
    std::vector<spdlog::sink_ptr> sinks;
    
    // Always add console sink with color
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(console_sink);
    
    // Add file sink if specified
    if (!log_file.empty()) {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
        sinks.push_back(file_sink);
    }
    
    if (!g_logger) {
        // Create new logger with all sinks
        g_logger = std::make_shared<spdlog::logger>("core", sinks.begin(), sinks.end());
        g_logger->set_pattern(pattern);
        
        // Flush on every log message to ensure immediate file writing
        g_logger->flush_on(spdlog::level::trace);
        
        // Register with spdlog
        spdlog::register_logger(g_logger);
    } else {
        // Logger already exists - need to recreate it with new sinks
        // Unregister the old one first
        spdlog::drop("core");
        
        // Create new logger with all sinks
        g_logger = std::make_shared<spdlog::logger>("core", sinks.begin(), sinks.end());
        g_logger->set_pattern(pattern);
        
        // Flush on every log message to ensure immediate file writing
        g_logger->flush_on(spdlog::level::trace);
        
        // Register the new one
        spdlog::register_logger(g_logger);
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
