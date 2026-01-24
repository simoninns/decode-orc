/*
 * File:        orc_analysis.h
 * Module:      orc-public
 * Purpose:     Public API for analysis tools and results
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace orc::public_api {

/**
 * @brief Information about an available analysis tool
 */
struct AnalysisToolInfo {
    std::string id;                         ///< Unique tool identifier
    std::string name;                       ///< Human-readable name
    std::string description;                ///< Description of what the tool does
    std::string category;                   ///< Category for organization
    int priority;                           ///< Menu ordering priority (lower = first)
    std::vector<std::string> applicable_stages; ///< Stage types this tool can analyze
};

/**
 * @brief Analysis operation status
 */
enum class AnalysisStatus {
    NotStarted,     ///< Analysis not yet started
    Running,        ///< Analysis in progress
    Complete,       ///< Analysis completed successfully
    Failed,         ///< Analysis failed with error
    Cancelled       ///< Analysis was cancelled
};

/**
 * @brief Progress information for running analysis
 */
struct AnalysisProgress {
    int current;                ///< Current progress value
    int total;                  ///< Total work units
    std::string status_message; ///< Primary status message
    std::string sub_status;     ///< Secondary status message
    AnalysisStatus status;      ///< Current status
};

} // namespace orc::public_api
