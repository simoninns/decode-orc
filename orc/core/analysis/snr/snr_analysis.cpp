/*
 * File:        snr_analysis.cpp
 * Module:      orc-core
 * Purpose:     SNR analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "snr_analysis.h"
#include "../analysis_registry.h"

namespace orc {

std::string SNRAnalysisTool::id() const {
    return "snr_analysis";
}

std::string SNRAnalysisTool::name() const {
    return "SNR Analysis";
}

std::string SNRAnalysisTool::description() const {
    return "Analyze signal-to-noise ratio statistics across all fields from stage output";
}

std::string SNRAnalysisTool::category() const {
    return "Analysis";
}

std::vector<ParameterDescriptor> SNRAnalysisTool::parameters() const {
    // Parameters could include analysis mode (white/black/both)
    return {};
}

bool SNRAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Works with field sources that have SNR observation data
    (void)source_type;
    return true;
}

std::string SNRAnalysisTool::decoder_name() const {
    return "SNRAnalysisDecoder";
}

// Auto-register this tool
REGISTER_ANALYSIS_TOOL(SNRAnalysisTool);

} // namespace orc
