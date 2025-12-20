/*
 * File:        analysis_init.h
 * Module:      orc-core/analysis
 * Purpose:     Analysis tool initialization header
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_INIT_H
#define ORC_CORE_ANALYSIS_INIT_H

namespace orc {

/**
 * @brief Force linking of all analysis tool object files
 * 
 * This function must be called before any analysis tool lookups occur
 * to ensure all analysis tools are properly registered.
 */
void force_analysis_tool_linking();

} // namespace orc

#endif // ORC_CORE_ANALYSIS_INIT_H
