/*
 * File:        dropout_analysis.cpp
 * Module:      orc-core
 * Purpose:     Dropout analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dropout_analysis.h"
#include "../analysis_registry.h"

namespace orc {

std::string DropoutAnalysisTool::id() const {
    return "dropout_analysis";
}

std::string DropoutAnalysisTool::name() const {
    return "Dropout Analysis";
}

std::string DropoutAnalysisTool::description() const {
    return "Analyze dropout statistics across all fields from stage output";
}

std::string DropoutAnalysisTool::category() const {
    return "Analysis";
}

std::vector<ParameterDescriptor> DropoutAnalysisTool::parameters() const {
    // Parameters could include analysis mode (full field vs visible area)
    return {};
}

bool DropoutAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Works with field sources that have dropout observation data
    (void)source_type;
    return true;
}

std::string DropoutAnalysisTool::decoder_name() const {
    return "DropoutAnalysisDecoder";
}

// Auto-register this tool
REGISTER_ANALYSIS_TOOL(DropoutAnalysisTool);

} // namespace orc
