/*
 * File:        crash_handler.h
 * Module:      orc-common
 * Purpose:     Minimal crash handler interface for CLI/GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <functional>

namespace orc {

struct CrashHandlerConfig {
    std::string application_name;
    std::string version;
    std::string output_directory;
    bool enable_coredump{false};
    bool auto_upload_info{false};
    std::function<std::string()> custom_info_callback;
};

bool init_crash_handler(const CrashHandlerConfig& config);
std::string create_crash_bundle(const std::string& description);
void cleanup_crash_handler();

} // namespace orc
