/*
 * File:        orc_stage_tooling.h
 * Module:      decode-orc Plugin SDK
 * Purpose:     Canonical stage helper/tooling contracts for plugin stages
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 *
 * STABILITY: PUBLIC — This header is part of the stable plugin SDK.
 */

#pragma once

#include <string>
#include <vector>

namespace orc {

// Canonical helper/tool categories that allow host GUI/CLI to route tooling
// requests without hardcoded stage-id branches.
enum class StageToolKind {
    ConfigDialog,
    NonModalEditor,
    BatchAnalysis,
    PreviewUtility,
    Custom
};

struct StageToolDescriptor {
    std::string tool_id;
    std::string display_name;
    std::string description;
    StageToolKind kind{StageToolKind::Custom};

    // If true, host may present this tool as a long-lived window/editor.
    bool non_modal{false};

    // Optional stable contract string for tool payload semantics.
    std::string contract_id;
};

// Canonical analysis-tool descriptor contract used when a stage advertises
// long-running/batch analysis capabilities.
struct AnalysisToolDescriptor {
    // Stable identifier consumed by host presenters and UI routing.
    std::string tool_id;

    // Human-friendly metadata for menus and dialogs.
    std::string display_name;
    std::string description;

    // Stable payload/execution contract string (for example,
    // "decode-orc.stage-tools.dropout-analysis.v1").
    std::string contract_id;

    // Whether host should automatically request analysis data after a trigger.
    bool auto_request_after_trigger{true};
};

// Optional mixin: stages can implement this to declare supported helper/tool
// integrations in a host-agnostic way.
class StageToolProvider {
public:
    virtual ~StageToolProvider() = default;
    virtual std::vector<StageToolDescriptor> get_stage_tools() const = 0;
};

// Optional mixin: stages can implement this to provide explicit analysis-tool
// contracts in addition to generic StageToolDescriptor metadata.
class AnalysisToolProvider {
public:
    virtual ~AnalysisToolProvider() = default;
    virtual std::vector<AnalysisToolDescriptor> get_analysis_tools() const = 0;
};

} // namespace orc
