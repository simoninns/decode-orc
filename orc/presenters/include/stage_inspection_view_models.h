/*
 * File:        stage_inspection_view_models.h
 * Module:      orc-presenters
 * Purpose:     Stage inspection view models for MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>

namespace orc::presenters {

/**
 * @brief Stage inspection report view model
 * 
 * Presents stage diagnostic information to the GUI layer.
 * This is a direct mapping of the core StageReport but in the
 * presenter namespace to maintain layer separation.
 */
struct StageInspectionView {
    std::string summary;  ///< Brief overview
    std::vector<std::pair<std::string, std::string>> items;  ///< Configuration items (label-value pairs)
    std::map<std::string, std::variant<int64_t, double, std::string>> metrics;  ///< Numeric/string metrics
};

} // namespace orc::presenters
