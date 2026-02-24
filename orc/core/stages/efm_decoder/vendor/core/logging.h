/*
 * File:        logging.h
 * Module:      EFM Decoder
 * Purpose:     Centralized logging using spdlog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <sstream>
#include <memory>
#include <string>
#include <cctype>
#include <algorithm>
#include <vector>

constexpr const char* CONSOLE_LOG_PATTERN = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v";
constexpr const char* FILE_LOG_PATTERN = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";

// Get or create the default logger
inline std::shared_ptr<spdlog::logger>& get_logger() {
    static std::shared_ptr<spdlog::logger> logger;
    if (!logger) {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(spdlog::level::info);
        consoleSink->set_pattern(CONSOLE_LOG_PATTERN);

        std::vector<spdlog::sink_ptr> sinks = {consoleSink};
        logger = std::make_shared<spdlog::logger>("efm-decoder", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        logger->set_pattern(CONSOLE_LOG_PATTERN);
        spdlog::set_default_logger(logger);
    }
    return logger;
}

inline bool parseLogLevel(const std::string &logLevel, spdlog::level::level_enum &level)
{
    std::string lowerLogLevel = logLevel;
    std::transform(lowerLogLevel.begin(), lowerLogLevel.end(), lowerLogLevel.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lowerLogLevel == "trace") {
        level = spdlog::level::trace;
        return true;
    }
    if (lowerLogLevel == "debug") {
        level = spdlog::level::debug;
        return true;
    }
    if (lowerLogLevel == "info") {
        level = spdlog::level::info;
        return true;
    }
    if (lowerLogLevel == "warn" || lowerLogLevel == "warning") {
        level = spdlog::level::warn;
        return true;
    }
    if (lowerLogLevel == "error") {
        level = spdlog::level::err;
        return true;
    }
    if (lowerLogLevel == "critical") {
        level = spdlog::level::critical;
        return true;
    }
    if (lowerLogLevel == "off") {
        level = spdlog::level::off;
        return true;
    }

    return false;
}

inline bool configureLogging(const std::string &logLevel = "info", bool quiet = false, const std::string &logFile = "")
{
    spdlog::level::level_enum consoleLevel;
    if (!parseLogLevel(logLevel, consoleLevel)) {
        return false;
    }

    if (quiet && consoleLevel < spdlog::level::info) {
        consoleLevel = spdlog::level::info;
    }

    try {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(consoleLevel);
        consoleSink->set_pattern(CONSOLE_LOG_PATTERN);

        std::vector<spdlog::sink_ptr> sinks = {consoleSink};

        if (!logFile.empty()) {
            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
            fileSink->set_level(spdlog::level::debug);
            fileSink->set_pattern(FILE_LOG_PATTERN);
            sinks.push_back(fileSink);
        }

        auto logger = std::make_shared<spdlog::logger>("efm-decoder", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger);
        get_logger() = logger;

        return true;
    } catch (const spdlog::spdlog_ex &) {
        return false;
    }
}

inline void setLogLevel(spdlog::level::level_enum level)
{
    get_logger()->set_level(level);
}

// Utility function to set binary mode on stdin/stdout (Windows compatibility)
inline void setBinaryMode(bool enable = true) {
#ifdef _WIN32
    if (enable) {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
    }
#endif
    // On Unix/Linux/macOS, binary mode is the default
}

// Utility function for debug level configuration
inline void setDebug(bool enabled) {
    if (enabled) {
        setLogLevel(spdlog::level::debug);
    } else {
        setLogLevel(spdlog::level::info);
    }
}

#define LOG_TRACE(...) get_logger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) get_logger()->debug(__VA_ARGS__)
#define LOG_INFO(...) get_logger()->info(__VA_ARGS__)
#define LOG_WARN(...) get_logger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) get_logger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) get_logger()->critical(__VA_ARGS__)

#endif // LOGGING_H
