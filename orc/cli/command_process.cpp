/*
 * File:        command_process.cpp
 * Module:      orc-cli
 * Purpose:     Process DAG command
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "command_process.h"
#include "project_presenter.h"
#include "logging.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace orc {
namespace cli {

int process_command(const ProcessOptions& options) {
    // Check if file exists
    if (!fs::exists(options.project_path)) {
        ORC_LOG_ERROR("Project file not found: {}", options.project_path);
        return 1;
    }
    
    ORC_LOG_INFO("Loading project: {}", options.project_path);
    
    // Create presenter and load project
    orc::presenters::ProjectPresenter presenter;
    try {
        if (!presenter.loadProject(options.project_path)) {
            ORC_LOG_ERROR("Failed to load project: {}", options.project_path);
            return 1;
        }
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to load project: {}", e.what());
        return 1;
    }
    
    ORC_LOG_INFO("Project loaded: {}", presenter.getProjectName());
    if (!presenter.getProjectDescription().empty()) {
        ORC_LOG_DEBUG("Project description: {}", presenter.getProjectDescription());
    }
    auto nodes = presenter.getNodes();
    auto edges = presenter.getEdges();
    ORC_LOG_DEBUG("Project contains {} nodes and {} edges", nodes.size(), edges.size());
    
    // Set up progress callback for console output
    size_t last_percent = 0;
    auto progress_callback = [&last_percent](size_t current, size_t total, const std::string& message) {
        if (total > 0) {
            size_t percent = (current * 100) / total;
            // Only log on significant progress change (every 5%)
            if (percent >= last_percent + 5 || current == total) {
                ORC_LOG_INFO("[Progress: {}%] {}", percent, message);
                last_percent = percent;
            }
        }
    };
    
    // Trigger all sink nodes using presenter
    bool all_success = presenter.triggerAllSinks(progress_callback);
    
    if (all_success) {
        return 0;
    } else {
        return 1;
    }
}

} // namespace cli
} // namespace orc
