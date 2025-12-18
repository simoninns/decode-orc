// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "include/node_type.h"
#include <limits>
#include <vector>
#include <unordered_map>

namespace orc {

namespace {

// Registry of all known node types
const std::vector<NodeTypeInfo> NODE_TYPE_REGISTRY = {
    // SOURCE nodes - no inputs, produce outputs
    {
        NodeType::SOURCE,
        "Source",
        "Source",
        "TBC input source - automatically created when a source is added to the project",
        0, 0,  // No inputs allowed
        1, 1,  // Exactly one output
        false  // Not user-creatable (created automatically by core)
    },
    
    // TRANSFORM nodes - one input, one output
    {
        NodeType::TRANSFORM,
        "Passthrough",
        "Pass-through Simple",
        "Pass input to output unchanged (no-op stage for testing)",
        1, 1,  // Exactly one input
        1, 1,  // Exactly one output
        true   // User can add this node
    },
    {
        NodeType::TRANSFORM,
        "DropoutCorrect",
        "Dropout Correction",
        "Correct dropouts by replacing corrupted samples with data from other lines/fields",
        1, 1,  // Exactly one input
        1, 1,  // Exactly one output
        true   // User can add this node
    },
    
    // SINK nodes - consume inputs, no outputs
    // (None implemented yet - future: export to file, preview output)
    
    // SPLITTER nodes - one input, multiple outputs
    {
        NodeType::SPLITTER,
        "PassthroughSplitter",
        "Pass-through Splitter",
        "Duplicate input to multiple outputs (test stage for fanout patterns)",
        1, 1,  // Exactly one input
        3, 3,  // Exactly three outputs
        true   // User can add this node
    },
    
    // MERGER nodes - multiple inputs, one output
    {
        NodeType::MERGER,
        "PassthroughMerger",
        "Pass-through Merger",
        "Select first input from multiple inputs (test stage for merge patterns)",
        2, 8,  // 2 to 8 inputs
        1, 1,  // Exactly one output
        true   // User can add this node
    },
    
    // COMPLEX nodes - multiple inputs, multiple outputs
    {
        NodeType::COMPLEX,
        "PassthroughComplex",
        "Pass-through Complex",
        "Multiple inputs to multiple outputs (test stage for complex patterns)",
        2, 4,  // 2 to 4 inputs
        2, 4,  // 2 to 4 outputs (same as inputs)
        true   // User can add this node
    }
};

// Build lookup map for fast access by stage_name
std::unordered_map<std::string, const NodeTypeInfo*> build_lookup_map() {
    std::unordered_map<std::string, const NodeTypeInfo*> map;
    for (const auto& info : NODE_TYPE_REGISTRY) {
        map[info.stage_name] = &info;
    }
    return map;
}

const std::unordered_map<std::string, const NodeTypeInfo*> STAGE_NAME_LOOKUP = build_lookup_map();

} // anonymous namespace

const NodeTypeInfo* get_node_type_info(const std::string& stage_name) {
    auto it = STAGE_NAME_LOOKUP.find(stage_name);
    if (it != STAGE_NAME_LOOKUP.end()) {
        return it->second;
    }
    return nullptr;
}

const std::vector<NodeTypeInfo>& get_all_node_types() {
    return NODE_TYPE_REGISTRY;
}

bool is_connection_valid(const std::string& source_stage, const std::string& target_stage) {
    const NodeTypeInfo* source_info = get_node_type_info(source_stage);
    const NodeTypeInfo* target_info = get_node_type_info(target_stage);
    
    // Unknown stages are not allowed
    if (!source_info || !target_info) {
        return false;
    }
    
    // Source must have outputs
    if (source_info->max_outputs == 0) {
        return false;
    }
    
    // Target must have inputs
    if (target_info->max_inputs == 0) {
        return false;
    }
    
    return true;
}

} // namespace orc
