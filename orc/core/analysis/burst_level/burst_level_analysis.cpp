/*
 * File:        burst_level_analysis.cpp
 * Module:      orc-core
 * Purpose:     Burst level analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "burst_level_analysis.h"
#include "../analysis_registry.h"

namespace orc {

std::string BurstLevelAnalysisTool::id() const {
    return "burst_level_analysis";
}

std::string BurstLevelAnalysisTool::name() const {
    return "Burst Level Analysis";
}

std::string BurstLevelAnalysisTool::description() const {
    return "Analyze color burst level statistics across all fields from stage output";
}

std::string BurstLevelAnalysisTool::category() const {
    return "Analysis";
}

std::vector<ParameterDescriptor> BurstLevelAnalysisTool::parameters() const {
    // No parameters needed for basic burst level analysis
    return {};
}

bool BurstLevelAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Works with field sources that have burst level observation data
    (void)source_type;
    return true;
}

std::string BurstLevelAnalysisTool::decoder_name() const {
    return "BurstLevelAnalysisDecoder";
}

// Auto-register this tool
REGISTER_ANALYSIS_TOOL(BurstLevelAnalysisTool);

} // namespace orc
