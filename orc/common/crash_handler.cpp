/*
 * File:        crash_handler.cpp
 * Module:      orc-common
 * Purpose:     Minimal crash handler implementation for CLI/GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "include/crash_handler.h"

#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;

namespace orc {

static CrashHandlerConfig g_config;

bool init_crash_handler(const CrashHandlerConfig& config) {
    g_config = config;
    // Ensure output directory exists
    if (!g_config.output_directory.empty()) {
        fs::create_directories(g_config.output_directory);
    }
    return true;
}

std::string create_crash_bundle(const std::string& description) {
    try {
        auto ts = std::chrono::system_clock::now().time_since_epoch().count();
        std::string filename = g_config.application_name + "_crash_" + std::to_string(ts) + ".txt";
        fs::path out = g_config.output_directory.empty() ? fs::current_path() : fs::path(g_config.output_directory);
        out /= filename;

        std::ofstream ofs(out);
        ofs << "Application: " << g_config.application_name << "\n";
        ofs << "Version: " << g_config.version << "\n";
        ofs << "Description: " << description << "\n";
        if (g_config.custom_info_callback) {
            ofs << "Custom Info:\n" << g_config.custom_info_callback() << "\n";
        }
        ofs.close();
        return out.string();
    } catch (...) {
        return {};
    }
}

void cleanup_crash_handler() {
    // No-op for minimal implementation
}

} // namespace orc
