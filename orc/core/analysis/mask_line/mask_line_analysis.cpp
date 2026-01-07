/*
 * File:        mask_line_analysis.cpp
 * Module:      orc-core
 * Purpose:     Line masking configuration analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "mask_line_analysis.h"
#include "../analysis_registry.h"
#include "../../include/project.h"
#include "../../include/dag_executor.h"
#include "logging.h"
#include <sstream>
#include <algorithm>

namespace orc {

std::string MaskLineAnalysisTool::id() const {
    return "mask_line_config";
}

std::string MaskLineAnalysisTool::name() const {
    return "Configure Line Masking";
}

std::string MaskLineAnalysisTool::description() const {
    return "Configure line masking with convenient presets for common scenarios "
           "like hiding NTSC closed captions. Configuration is applied immediately.";
}

std::string MaskLineAnalysisTool::category() const {
    return "Configuration";
}

std::vector<ParameterDescriptor> MaskLineAnalysisTool::parameters() const {
    std::vector<ParameterDescriptor> params;
    
    // Checkbox for masking NTSC CC line
    ParameterDescriptor mask_ntsc_cc;
    mask_ntsc_cc.name = "maskNTSC_CC";
    mask_ntsc_cc.display_name = "Mask NTSC Closed Captions";
    mask_ntsc_cc.description = "Mask field line 20 in first field (NTSC closed caption - traditional 'line 21' is index 20)";
    mask_ntsc_cc.type = ParameterType::BOOL;
    mask_ntsc_cc.constraints.default_value = false;
    params.push_back(mask_ntsc_cc);
    
    // Checkbox for masking PAL Teletext/WSS
    ParameterDescriptor mask_pal_tt;
    mask_pal_tt.name = "maskPAL_TT";
    mask_pal_tt.display_name = "Mask PAL Teletext/WSS";
    mask_pal_tt.description = "Mask field lines 6-22 in both fields (PAL Teletext and WSS lines)";
    mask_pal_tt.type = ParameterType::BOOL;
    mask_pal_tt.constraints.default_value = false;
    params.push_back(mask_pal_tt);
    
    // Custom line specification (advanced)
    ParameterDescriptor custom_lines;
    custom_lines.name = "customLines";
    custom_lines.display_name = "Custom Line Spec";
    custom_lines.description = "Custom line specification with parity prefix (e.g., 'F:25', 'S:10-15', 'A:20'). "
                               "F=first field, S=second field, A=all fields. Leave empty to use only preset options above.";
    custom_lines.type = ParameterType::STRING;
    custom_lines.constraints.default_value = std::string("");
    params.push_back(custom_lines);
    
    // Mask IRE level
    ParameterDescriptor mask_ire;
    mask_ire.name = "maskIRE";
    mask_ire.display_name = "Mask IRE Level";
    mask_ire.description = "IRE level to write to masked pixels (0 = black, 100 = white)";
    mask_ire.type = ParameterType::DOUBLE;
    mask_ire.constraints.default_value = 0.0;
    mask_ire.constraints.min_value = ParameterValue{0.0};
    mask_ire.constraints.max_value = ParameterValue{100.0};
    params.push_back(mask_ire);
    
    return params;
}

std::vector<ParameterDescriptor> MaskLineAnalysisTool::parametersForContext(const AnalysisContext& ctx) const {
    // Get base parameters
    auto params = parameters();
    
    // Show/hide PAL or NTSC specific options based on video system
    // For now, just return all parameters since we don't have direct access to video system
    // The user can see all options and choose what's relevant
    (void)ctx;  // Unused for now
    
    return params;
}

bool MaskLineAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Can work with laserdisc sources
    return source_type == AnalysisSourceType::LaserDisc;
}

bool MaskLineAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    // This tool is applicable to mask_line stages
    return stage_name == "mask_line";
}

AnalysisResult MaskLineAnalysisTool::analyze(const AnalysisContext& ctx,
                                            AnalysisProgress* progress) {
    AnalysisResult result;
    
    // This is an instant configuration tool - no progress needed
    (void)progress;
    
    // Build the configuration based on checkboxes
    double mask_ire = 0.0;
    
    // Extract parameters
    bool mask_ntsc_cc = false;
    bool mask_pal_tt = false;
    std::string custom_lines;
    
    auto it = ctx.parameters.find("maskNTSC_CC");
    if (it != ctx.parameters.end() && std::holds_alternative<bool>(it->second)) {
        mask_ntsc_cc = std::get<bool>(it->second);
    }
    
    it = ctx.parameters.find("maskPAL_TT");
    if (it != ctx.parameters.end() && std::holds_alternative<bool>(it->second)) {
        mask_pal_tt = std::get<bool>(it->second);
    }
    
    it = ctx.parameters.find("customLines");
    if (it != ctx.parameters.end() && std::holds_alternative<std::string>(it->second)) {
        custom_lines = std::get<std::string>(it->second);
    }
    
    it = ctx.parameters.find("maskIRE");
    if (it != ctx.parameters.end() && std::holds_alternative<double>(it->second)) {
        mask_ire = std::get<double>(it->second);
    }
    
    // Build configuration
    std::string line_spec_result;
    
    if (mask_ntsc_cc) {
        // NTSC CC is on field line 20 (0-based index), first field only
        // Traditional "line 21" in 1-based video terminology = index 20
        line_spec_result = "F:20";
    } else if (mask_pal_tt) {
        // PAL Teletext/WSS is on field lines 6-22 (0-based), both fields
        line_spec_result = "A:6-22";
    } else if (!custom_lines.empty()) {
        // Custom configuration
        line_spec_result = custom_lines;
    } else {
        // No masking selected
        line_spec_result = "";
    }
    
    result.graphData["lineSpec"] = line_spec_result;
    
    std::ostringstream mask_ire_str;
    mask_ire_str << mask_ire;
    result.graphData["maskIRE"] = mask_ire_str.str();
    
    // Build summary showing what will be applied
    std::ostringstream summary;
    summary << "Configuration ready to apply:\n\n";
    summary << "Line Specification: ";
    if (line_spec_result.empty()) {
        summary << "(none - no lines will be masked)\n";
    } else {
        summary << line_spec_result << "\n";
        if (mask_ntsc_cc) {
            summary << "  → NTSC Closed Captions (field line 20, first field - traditional 'line 21')\n";
        } else if (mask_pal_tt) {
            summary << "  → PAL Teletext/WSS (field lines 6-22, both fields)\n";
        }
    }
    summary << "\nMask IRE Level: " << mask_ire << " IRE";
    if (mask_ire == 0.0) {
        summary << " (black)";
    } else if (mask_ire == 100.0) {
        summary << " (white)";
    }
    summary << "\n\n";
    
    if (!line_spec_result.empty()) {
        summary << "Click 'Apply to Node' to configure the Mask Line stage.";
    }
    
    result.summary = summary.str();
    result.status = AnalysisResult::Success;
    
    return result;
}

bool MaskLineAnalysisTool::canApplyToGraph() const {
    return true;
}

bool MaskLineAnalysisTool::applyToGraph(const AnalysisResult& result,
                                       Project& project,
                                       NodeID node_id) {
    // Apply the configuration to the mask_line stage
    try {
        // Get the node to modify parameters
        const auto& nodes = project.get_nodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
        
        if (node_it == nodes.end()) {
            ORC_LOG_ERROR("MaskLineAnalysisTool: node not found: {}", node_id);
            return false;
        }
        
        // Build updated parameters
        auto updated_params = node_it->parameters;
        
        auto data_it = result.graphData.find("lineSpec");
        if (data_it != result.graphData.end()) {
            updated_params["lineSpec"] = data_it->second;
        }
        
        data_it = result.graphData.find("maskIRE");
        if (data_it != result.graphData.end()) {
            // Convert string back to double
            try {
                double mask_ire = std::stod(data_it->second);
                updated_params["maskIRE"] = mask_ire;
            } catch (const std::exception& e) {
                ORC_LOG_WARN("Failed to parse maskIRE: {}", e.what());
            }
        }
        
        // Apply the parameters
        project_io::set_node_parameters(project, node_id, updated_params);
        
        ORC_LOG_INFO("Applied line masking configuration to node '{}'", node_id);
        return true;
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to apply line masking configuration: {}", e.what());
        return false;
    }
}

// Register the tool
REGISTER_ANALYSIS_TOOL(MaskLineAnalysisTool);

} // namespace orc
