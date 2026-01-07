/*
 * File:        command_process.h
 * Module:      orc-cli
 * Purpose:     Process DAG command header
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <string>

namespace orc {
namespace cli {

/**
 * @brief Options for the process command
 * 
 * Contains configuration for processing a complete DAG by triggering all sink nodes.
 */
struct ProcessOptions {
    std::string project_path;  ///< Path to the .orcprj project file
};

/**
 * @brief Execute the process command
 * 
 * Loads the specified project file, converts it to a DAG, and triggers all
 * sink nodes to process the complete pipeline. This is the main execution
 * path for batch processing.
 * 
 * @param options Configuration options including project path
 * @return Exit code (0 = success, non-zero = error)
 */
int process_command(const ProcessOptions& options);

} // namespace cli
} // namespace orc
