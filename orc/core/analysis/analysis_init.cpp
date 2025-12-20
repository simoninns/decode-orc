/*
 * File:        analysis_init.cpp
 * Module:      orc-core/analysis
 * Purpose:     Analysis tool initialization
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "field_mapping/field_mapping_analysis.h"
#include "logging.h"
#include <memory>

namespace orc {

/**
 * @brief Force linking of all analysis tool object files
 * 
 * This function creates dummy instances to force the linker to include
 * all analysis tool object files, which ensures their static registrations execute.
 * This must be called before any analysis tool lookups occur.
 */
void force_analysis_tool_linking() {
    ORC_LOG_DEBUG("Forcing analysis tool linking...");
    // Create dummy instances to force vtable instantiation
    // This ensures the object files are linked and static initializers run
    [[maybe_unused]] auto dummy1 = std::make_unique<FieldMappingAnalysisTool>();
    ORC_LOG_DEBUG("Analysis tool linking complete");
}

} // namespace orc
