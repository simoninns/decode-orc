/*
 * File:        dropout_editor_tool.h
 * Module:      orc-core
 * Purpose:     Dropout map editor analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_DROPOUT_EDITOR_TOOL_H
#define ORC_CORE_DROPOUT_EDITOR_TOOL_H

#include "analysis_tool.h"
#include "analysis_context.h"
#include "analysis_result.h"
#include "../include/stage_parameter.h"
#include <string>
#include <vector>

namespace orc {

/**
 * @brief Analysis tool for editing dropout maps
 * 
 * This tool opens a GUI dialog that allows the user to:
 * - Navigate through video fields
 * - Mark new dropout regions by clicking and dragging
 * - Remove false positive dropout regions
 * - Save changes back to the dropout map stage parameter
 * 
 * This is a GUI-only tool that is triggered from the analysis menu
 * on a dropout_map stage node.
 */
class DropoutEditorTool : public AnalysisTool {
public:
    DropoutEditorTool() = default;
    ~DropoutEditorTool() override = default;

    // AnalysisTool interface
    std::string id() const override {
        return "dropout_editor";
    }

    std::string name() const override {
        return "Edit Dropout Map";
    }

    std::string description() const override {
        return "Interactive editor for marking and removing dropout regions";
    }

    std::string category() const override {
        return "Dropout";
    }

    std::vector<ParameterDescriptor> parameters() const override {
        // This tool doesn't have parameters - it's purely GUI-driven
        return {};
    }

    bool canAnalyze(AnalysisSourceType source_type) const override {
        // This tool works with LaserDisc sources (which provide video fields)
        return source_type == AnalysisSourceType::LaserDisc;
    }

    bool isApplicableToStage(const std::string& stage_name) const override {
        // This tool is only applicable to the dropout_map stage
        return stage_name == "dropout_map";
    }

    int priority() const override {
        // Priority 1 = stage-specific tool (appears first in menu)
        return 1;
    }

    AnalysisResult analyze(const AnalysisContext& ctx,
                          AnalysisProgress* progress) override;

    bool canApplyToGraph() const override {
        // This tool modifies the stage parameters directly
        return true;
    }

    bool applyToGraph(const AnalysisResult& result,
                     Project& project,
                     NodeID node_id) override;
};

} // namespace orc

#endif // ORC_CORE_DROPOUT_EDITOR_TOOL_H
