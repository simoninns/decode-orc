/*
 * File:        batch_analysis_tool.cpp
 * Module:      orc-core
 * Purpose:     Base class for GUI-triggered batch analysis tools implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "batch_analysis_tool.h"
#include "logging.h"

namespace orc {

AnalysisResult BatchAnalysisTool::analyze(const AnalysisContext& ctx,
                                         AnalysisProgress* progress) {
    (void)ctx;  // Unused - GUI-triggered tool
    
    AnalysisResult result;
    
    // This is a batch analysis tool that will be triggered via the GUI
    // The actual data processing happens in the RenderCoordinator and the
    // associated decoder (e.g., DropoutAnalysisDecoder, SNRAnalysisDecoder)
    // This method exists to satisfy the AnalysisTool interface and for future
    // command-line batch processing support
    
    if (progress) {
        std::string status_message = name() + " will be processed via GUI";
        progress->setStatus(status_message);
        progress->setProgress(100);
    }
    
    result.status = AnalysisResult::Success;
    result.summary = name() + " tool registered";
    
    ORC_LOG_DEBUG("{} registered (GUI-triggered batch processing via {})",
                  name(), decoder_name());
    
    return result;
}

} // namespace orc
