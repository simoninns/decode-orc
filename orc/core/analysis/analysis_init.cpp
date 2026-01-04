/*
 * File:        analysis_init.cpp
 * Module:      orc-core/analysis
 * Purpose:     Analysis tool initialization
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "field_mapping/field_mapping_analysis.h"
#include "field_mapping/field_mapping_range_analysis.h"
#include "field_corruption/field_corruption_analysis.h"
#include "vectorscope/vectorscope_analysis.h"
#include "dropout/dropout_analysis.h"
#include "dropout_editor_tool.h"
#include "snr/snr_analysis.h"
#include "burst_level/burst_level_analysis.h"
#include "source_alignment/source_alignment_analysis.h"
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
    [[maybe_unused]] auto dummy2 = std::make_unique<FieldMappingRangeAnalysisTool>();
    [[maybe_unused]] auto dummy3 = std::make_unique<FieldCorruptionAnalysisTool>();
    [[maybe_unused]] auto dummy4 = std::make_unique<VectorscopeAnalysisTool>();
    [[maybe_unused]] auto dummy5 = std::make_unique<DropoutAnalysisTool>();
    [[maybe_unused]] auto dummy6 = std::make_unique<DropoutEditorTool>();
    [[maybe_unused]] auto dummy7 = std::make_unique<SNRAnalysisTool>();
    [[maybe_unused]] auto dummy8 = std::make_unique<BurstLevelAnalysisTool>();
    [[maybe_unused]] auto dummy9 = std::make_unique<SourceAlignmentAnalysisTool>();
    ORC_LOG_DEBUG("Analysis tool linking complete");
}

} // namespace orc
