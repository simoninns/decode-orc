/*
 * File:        dropout_editor_tool.cpp
 * Module:      orc-core
 * Purpose:     Dropout map editor analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dropout_editor_tool.h"
#include "analysis_registry.h"
#include "logging.h"

namespace orc {

// Force linker to include this object file (for static registration)
void force_link_DropoutEditorTool() {}

AnalysisResult DropoutEditorTool::analyze(const AnalysisContext& ctx,
                                         AnalysisProgress* progress) {
    (void)ctx;  // Context is used by the GUI
    
    AnalysisResult result;
    
    // This is a GUI-triggered tool - the actual editor dialog is opened
    // by the MainWindow when this tool is selected from the analysis menu.
    // This method exists to satisfy the AnalysisTool interface.
    
    if (progress) {
        progress->setStatus("Dropout Map Editor opened via GUI");
        progress->setProgress(100);
    }
    
    result.status = AnalysisResult::Success;
    result.summary = "Dropout Map Editor tool registered (GUI mode)";
    
    ORC_LOG_DEBUG("DropoutEditorTool registered for dropout_map stages");
    
    return result;
}

bool DropoutEditorTool::applyToGraph(AnalysisResult& result,
                                    const Project& project,
                                    NodeID node_id) {
    (void)result;
    (void)project;
    (void)node_id;
    
    // The GUI dialog applies changes directly to the stage parameters
    // when the user clicks OK, so there's nothing to do here.
    
    return true;
}

// Register the tool
REGISTER_ANALYSIS_TOOL(DropoutEditorTool)

} // namespace orc
