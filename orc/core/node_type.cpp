/*
 * File:        node_type.cpp
 * Module:      orc-core
 * Purpose:     Node type registry
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "include/node_type.h"
#include "include/stage_registry.h"
#include "logging.h"
#include <limits>
#include <vector>
#include <unordered_map>

namespace orc {

const NodeTypeInfo* get_node_type_info(const std::string& stage_name) {
    // Create a temporary instance of the stage to query its metadata
    auto& registry = StageRegistry::instance();
    
    if (!registry.has_stage(stage_name)) {
        auto stages = registry.get_registered_stages();
        std::string available_stages;
        for (size_t i = 0; i < stages.size(); ++i) {
            available_stages += stages[i];
            if (i < stages.size() - 1) available_stages += ", ";
        }
        ORC_LOG_ERROR("Stage '{}' is not registered. Available: {}", stage_name, available_stages);
        return nullptr;
    }
    
    try {
        auto stage = registry.create_stage(stage_name);
        static thread_local std::unordered_map<std::string, NodeTypeInfo> cache;
        
        // Cache the result to avoid repeated stage creation
        auto it = cache.find(stage_name);
        if (it == cache.end()) {
            cache[stage_name] = stage->get_node_type_info();
            it = cache.find(stage_name);
        }
        
        return &it->second;
    } catch (...) {
        return nullptr;
    }
}

const std::vector<NodeTypeInfo>& get_all_node_types() {
    static std::vector<NodeTypeInfo> all_types;
    static bool initialized = false;
    
    if (!initialized) {
        auto& registry = StageRegistry::instance();
        auto stage_names = registry.get_registered_stages();
        
        for (const auto& name : stage_names) {
            const NodeTypeInfo* info = get_node_type_info(name);
            if (info) {
                all_types.push_back(*info);
            }
        }
        
        initialized = true;
    }
    
    return all_types;
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
