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

struct AnalyzeFieldMappingOptions {
    std::string project_path;
    bool update_project = false;
    bool pad_gaps = true;
    bool delete_unmappable = false;
};

int analyze_field_mapping_command(const AnalyzeFieldMappingOptions& options);

} // namespace cli
} // namespace orc
