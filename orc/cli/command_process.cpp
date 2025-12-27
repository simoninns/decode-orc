/*
 * File:        command_process.cpp
 * Module:      orc-cli
 * Purpose:     Process DAG command
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "command_process.h"
#include "project.h"
#include "project_to_dag.h"
#include "dag_executor.h"
#include "stage_registry.h"
#include "ld_sink_stage.h"
#include "logging.h"

#include <iostream>
#include <memory>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace orc {
namespace cli {

int process_command(const ProcessOptions& options) {
    // Check if file exists
    if (!fs::exists(options.project_path)) {
        ORC_LOG_ERROR("Project file not found: {}", options.project_path);
        return 1;
    }
    
    ORC_LOG_INFO("Loading project: {}", options.project_path);
    
    // Load project
    Project project;
    try {
        project = orc::project_io::load_project(options.project_path);
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to load project: {}", e.what());
        return 1;
    }
    
    ORC_LOG_INFO("Project loaded: {} (version {})", project.get_name(), project.get_version());
    if (!project.get_description().empty()) {
        ORC_LOG_INFO("Project description: {}", project.get_description());
    }
    ORC_LOG_INFO("Project contains {} nodes and {} edges", 
                 project.get_nodes().size(), project.get_edges().size());
    
    // Convert project to DAG
    auto dag = orc::project_to_dag(project);
    if (!dag) {
        ORC_LOG_ERROR("Failed to convert project to DAG");
        return 1;
    }
    
    // Find all sink nodes (triggerable stages)
    std::vector<std::string> sink_nodes;
    auto& registry = StageRegistry::instance();
    
    for (const auto& node : project.get_nodes()) {
        if (!registry.has_stage(node.stage_name)) {
            ORC_LOG_WARN("Unknown stage type: {}", node.stage_name);
            continue;
        }
        
        auto stage = registry.create_stage(node.stage_name);
        if (!stage) {
            ORC_LOG_WARN("Failed to create stage: {}", node.stage_name);
            continue;
        }
        
        auto* trigger_stage = dynamic_cast<TriggerableStage*>(stage.get());
        if (trigger_stage) {
            sink_nodes.push_back(node.node_id);
            ORC_LOG_INFO("Found triggerable node: {} ({})", node.node_id, node.stage_name);
        }
    }
    
    if (sink_nodes.empty()) {
        ORC_LOG_ERROR("No triggerable sink nodes found in project");
        return 1;
    }
    
    // Trigger each sink node
    bool all_success = true;
    for (const auto& node_id : sink_nodes) {
        ORC_LOG_INFO("========================================");
        ORC_LOG_INFO("Triggering node: {}", node_id);
        ORC_LOG_INFO("========================================");
        
        // Find node in DAG
        const auto& nodes = dag->nodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const DAGNode& n) { return n.node_id == node_id; });
        
        if (node_it == nodes.end()) {
            ORC_LOG_ERROR("Node '{}' not found in DAG", node_id);
            all_success = false;
            continue;
        }
        
        // The DAG node already has the stage instance
        auto* trigger_stage = dynamic_cast<TriggerableStage*>(node_it->stage.get());
        if (!trigger_stage) {
            ORC_LOG_ERROR("Node '{}' is not triggerable", node_id);
            all_success = false;
            continue;
        }
        
        // Execute DAG to collect inputs
        ORC_LOG_INFO("Executing DAG to collect {} input nodes", node_it->input_node_ids.size());
        DAGExecutor executor;
        
        std::vector<ArtifactPtr> inputs;
        bool inputs_failed = false;
        for (const auto& input_node_id : node_it->input_node_ids) {
            try {
                ORC_LOG_DEBUG("Executing to input node '{}'", input_node_id);
                auto results = executor.execute_to_node(*dag, input_node_id);
                
                auto input_it = results.find(input_node_id);
                if (input_it != results.end() && !input_it->second.empty()) {
                    ORC_LOG_DEBUG("Collected input from node '{}': {} outputs", 
                                 input_node_id, input_it->second.size());
                    inputs.push_back(input_it->second[0]);
                } else {
                    ORC_LOG_ERROR("No output found for input node '{}'", input_node_id);
                    inputs_failed = true;
                    break;
                }
            } catch (const std::exception& e) {
                ORC_LOG_ERROR("Failed to execute node '{}': {}", input_node_id, e.what());
                inputs_failed = true;
                break;
            }
        }
        
        if (inputs_failed) {
            all_success = false;
            continue;
        }
        
        if (inputs.size() != node_it->input_node_ids.size()) {
            ORC_LOG_ERROR("Failed to collect all inputs for node '{}'", node_id);
            all_success = false;
            continue;
        }
        
        // Trigger the stage
        try {
            ORC_LOG_INFO("Calling trigger() with {} inputs", inputs.size());
            
            // Set up progress callback
            std::string last_message;
            size_t last_percent = 0;
            auto progress_callback = [&last_message, &last_percent](size_t current, size_t total, const std::string& message) {
                if (total > 0) {
                    size_t percent = (current * 100) / total;
                    // Only log on message change or significant progress change
                    if (message != last_message || percent >= last_percent + 5) {
                        std::cout << "[Progress: " << percent << "%] " << message << std::endl;
                        last_message = message;
                        last_percent = percent;
                    }
                }
            };
            
            trigger_stage->set_progress_callback(progress_callback);
            
            bool success = trigger_stage->trigger(inputs, node_it->parameters);
            
            if (success) {
                std::string status = trigger_stage->get_trigger_status();
                ORC_LOG_INFO("Trigger SUCCESS: {}", status);
            } else {
                std::string status = trigger_stage->get_trigger_status();
                ORC_LOG_ERROR("Trigger FAILED: {}", status);
                all_success = false;
            }
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Trigger failed with exception: {}", e.what());
            all_success = false;
        }
    }
    
    if (all_success) {
        ORC_LOG_INFO("========================================");
        ORC_LOG_INFO("All sink nodes triggered successfully");
        ORC_LOG_INFO("========================================");
        return 0;
    } else {
        ORC_LOG_ERROR("========================================");
        ORC_LOG_ERROR("One or more sink nodes failed");
        ORC_LOG_ERROR("========================================");
        return 1;
    }
}

} // namespace cli
} // namespace orc
