/*
 * File:        command_analyze_source_aligns.h
 * Module:      orc-cli
 * Purpose:     Analyze source alignment command header
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include <string>

namespace orc {
namespace cli {

/**
 * @brief Options for the source alignment analysis command
 * 
 * Configures source alignment analysis for synchronizing multiple video sources.
 */
struct AnalyzeSourceAlignsOptions {
    std::string project_path;  ///< Path to the .orcprj project file
};

/**
 * @brief Execute the source alignment analysis command
 * 
 * Analyzes all source_align stages in the project to determine optimal
 * frame alignment between multiple video sources. Updates the project file
 * with computed alignment maps.
 * 
 * @param options Configuration options including project path
 * @return Exit code (0 = success, non-zero = error)
 */
int analyze_source_aligns_command(const AnalyzeSourceAlignsOptions& options);

} // namespace cli
} // namespace orc
