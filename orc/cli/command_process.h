/*
 * File:        command_process.h
 * Module:      orc-cli
 * Purpose:     Process DAG command header
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include <string>

namespace orc {
namespace cli {

struct ProcessOptions {
    std::string project_path;
};

int process_command(const ProcessOptions& options);

} // namespace cli
} // namespace orc
