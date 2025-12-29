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

struct AnalyzeSourceAlignsOptions {
    std::string project_path;
};

int analyze_source_aligns_command(const AnalyzeSourceAlignsOptions& options);

} // namespace cli
} // namespace orc
