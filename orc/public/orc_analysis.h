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
#include <map>
#include <variant>
#include <parameter_types.h>

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

/**
 * @brief Type of source being analyzed
 */
enum class AnalysisSourceType {
    LaserDisc,
    CVBSVideo,
    Other
};

/**
 * @brief Statistic value that can be displayed
 */
using StatisticValue = std::variant<bool, int, long long, double, std::string>;

/**
 * @brief Individual result item from analysis
 */
struct AnalysisResultItem {
    std::string type;           ///< Type: "skip", "repeat", "gap", "warning", etc.
    std::string message;        ///< Human-readable description
    int startFrame = -1;        ///< Start frame (-1 if not applicable)
    int endFrame = -1;          ///< End frame (-1 if not applicable)
    std::map<std::string, StatisticValue> metadata;  ///< Tool-specific data
};

/**
 * @brief Complete analysis result
 */
struct AnalysisResult {
    enum class Status {
        Success,
        Failed,
        Cancelled
    };
    
    Status status = Status::Success;
    std::string summary;                                    ///< Human-readable summary
    std::vector<AnalysisResultItem> items;                  ///< Structured results
    std::map<std::string, StatisticValue> statistics;       ///< Statistics for display
    std::map<std::string, std::string> graphData;           ///< Data for graph application (opaque to GUI)
    std::map<std::string, orc::ParameterValue> parameterChanges;  ///< Parameter modifications to apply
};

} // namespace orc::public_api
