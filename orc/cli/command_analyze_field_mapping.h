/*
 * File:        command_analyze_field_mapping.h
 * Module:      orc-cli
 * Purpose:     Analyze field mapping command header
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include <string>

namespace orc {
namespace cli {

/**
 * @brief Options for the field mapping analysis command
 * 
 * Configures how field mapping analysis should be performed and whether
 * the results should be written back to the project file.
 */
struct AnalyzeFieldMappingOptions {
    std::string project_path;      ///< Path to the .orcprj project file
    bool update_project = false;   ///< Whether to update project with analysis results
    bool pad_gaps = true;          ///< Whether to pad gaps with black frames
    bool delete_unmappable = false; ///< Whether to delete frames that can't be mapped
};

/**
 * @brief Execute the field mapping analysis command
 * 
 * Analyzes all field_map stages in the project to determine optimal field
 * mapping configurations. Can optionally update the project file with the
 * computed mappings.
 * 
 * @param options Configuration options including project path and analysis behavior
 * @return Exit code (0 = success, non-zero = error)
 */
int analyze_field_mapping_command(const AnalyzeFieldMappingOptions& options);

} // namespace cli
} // namespace orc
