/*
 * File:        core_init.cpp
 * Module:      orc-presenters
 * Purpose:     Core initialization functions for presenters layer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/project_presenter.h"
#include "../core/include/logging.h"

namespace orc::presenters {

void initCoreLogging(const std::string& level, const std::string& pattern, const std::string& log_file) {
    orc::init_logging(level, pattern, log_file);
}

} // namespace orc::presenters
