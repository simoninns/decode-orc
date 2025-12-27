/*
 * File:        dag_executor.cpp
 * Module:      orc-core
 * Purpose:     DAG execution engine
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "dag_executor.h"
#include "logging.h"
#include <algorithm>
#include <set>
#include <queue>
#include <sstream>

namespace orc {

// ============================================================================
// DAG Implementation
// ============================================================================

void DAG::add_node(DAGNode node) {
    nodes_.push_back(std::move(node));
}

void DAG::set_root_inputs(const std::vector<ArtifactPtr>& inputs) {
    root_inputs_ = inputs;
}

void DAG::set_output_nodes(const std::vector<std::string>& node_ids) {
    output_node_ids_ = node_ids;
}

std::map<std::string, size_t> DAG::build_node_index() const {
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        index[nodes_[i].node_id] = i;
    }
    return index;
}

bool DAG::validate() const {
    return get_validation_errors().empty();
}

std::vector<std::string> DAG::get_validation_errors() const {
    std::vector<std::string> errors;
    
    // Build node index
    auto node_index = build_node_index();
    
    // Check for duplicate node IDs
    if (node_index.size() != nodes_.size()) {
        errors.push_back("Duplicate node IDs detected");
    }
    
    // Check that all input dependencies exist
    for (const auto& node : nodes_) {
        for (const auto& input_id : node.input_node_ids) {
            if (node_index.find(input_id) == node_index.end()) {
                errors.push_back("Node '" + node.node_id + "' depends on non-existent node '" + input_id + "'");
            }
        }
    }
    
    // Check for cycles
    if (has_cycle()) {
        errors.push_back("DAG contains a cycle");
    }
    
    // Check output nodes exist
    for (const auto& output_id : output_node_ids_) {
        if (node_index.find(output_id) == node_index.end()) {
            errors.push_back("Output node '" + output_id + "' does not exist");
        }
    }
    
    return errors;
}

bool DAG::has_cycle() const {
    auto node_index = build_node_index();
    std::map<std::string, int> state;  // 0=unvisited, 1=visiting, 2=visited
    
    std::function<bool(const std::string&)> visit = [&](const std::string& node_id) -> bool {
        if (state[node_id] == 1) return true;  // Cycle detected
        if (state[node_id] == 2) return false; // Already visited
        
        state[node_id] = 1;  // Mark as visiting
        
        auto it = node_index.find(node_id);
        if (it != node_index.end()) {
            const auto& node = nodes_[it->second];
            for (const auto& input_id : node.input_node_ids) {
                if (visit(input_id)) return true;
            }
        }
        
        state[node_id] = 2;  // Mark as visited
        return false;
    };
    
    for (const auto& node : nodes_) {
        if (state[node.node_id] == 0) {
            if (visit(node.node_id)) return true;
        }
    }
    
    return false;
}

// ============================================================================
// DAGExecutor Implementation
// ============================================================================

std::vector<ArtifactPtr> DAGExecutor::execute(const DAG& dag) {
    if (!dag.validate()) {
        auto errors = dag.get_validation_errors();
        std::ostringstream oss;
        oss << "DAG validation failed:\n";
        for (const auto& error : errors) {
            oss << "  - " << error << "\n";
        }
        throw DAGExecutionError(oss.str());
    }
    
    // Topological sort
    auto execution_order = topological_sort(dag);
    
    // Execute nodes in order
    std::map<std::string, std::vector<ArtifactPtr>> node_outputs;
    
    // Initialize with root inputs (virtual node)
    node_outputs["__root__"] = dag.root_inputs();
    
    size_t total_nodes = execution_order.size();
    size_t current_node = 0;
    
    auto node_index = dag.build_node_index();
    
    for (const auto& node_id : execution_order) {
        ++current_node;
        
        ORC_LOG_DEBUG("Node '{}': Executing ({}/{} in order)", node_id, current_node, total_nodes);
        
        if (progress_callback_) {
            progress_callback_(node_id, current_node, total_nodes);
        }
        
        const auto& node = dag.nodes()[node_index[node_id]];
        
        // Gather inputs
        std::vector<ArtifactPtr> inputs;
        
        if (node.input_node_ids.empty()) {
            // Root node with no dependencies (e.g., SOURCE nodes)
            // No inputs needed - stage generates output
            inputs = {};
        } else {
            for (size_t i = 0; i < node.input_node_ids.size(); ++i) {
                const auto& input_node_id = node.input_node_ids[i];
                size_t output_index = i < node.input_indices.size() ? node.input_indices[i] : 0;
                
                auto it = node_outputs.find(input_node_id);
                if (it == node_outputs.end() || output_index >= it->second.size()) {
                    throw DAGExecutionError("Missing input for node '" + node_id + "' from '" + input_node_id + "'");
                }
                
                inputs.push_back(it->second[output_index]);
            }
        }
        
        // Execute or retrieve from cache
        auto outputs = get_cached_or_execute(node, inputs);
        node_outputs[node_id] = {outputs};
    }
    
    // Gather output artifacts
    std::vector<ArtifactPtr> results;
    for (const auto& output_id : dag.output_nodes()) {
        auto it = node_outputs.find(output_id);
        if (it != node_outputs.end() && !it->second.empty()) {
            results.push_back(it->second[0]);
        }
    }
    
    return results;
}

std::vector<std::string> DAGExecutor::topological_sort(const DAG& dag) const {
    auto node_index = dag.build_node_index();
    
    // Calculate in-degrees
    std::map<std::string, size_t> in_degree;
    for (const auto& node : dag.nodes()) {
        in_degree[node.node_id] = 0;
    }
    for (const auto& node : dag.nodes()) {
        for (const auto& input_id : node.input_node_ids) {
            in_degree[input_id]++;
        }
    }
    
    // Kahn's algorithm
    std::queue<std::string> queue;
    for (const auto& [node_id, degree] : in_degree) {
        if (degree == 0) {
            queue.push(node_id);
        }
    }
    
    std::vector<std::string> result;
    while (!queue.empty()) {
        std::string node_id = queue.front();
        queue.pop();
        result.push_back(node_id);
        
        const auto& node = dag.nodes()[node_index[node_id]];
        for (const auto& input_id : node.input_node_ids) {
            in_degree[input_id]--;
            if (in_degree[input_id] == 0) {
                queue.push(input_id);
            }
        }
    }
    
    // Reverse for execution order (dependencies first)
    std::reverse(result.begin(), result.end());
    
    return result;
}

ArtifactPtr DAGExecutor::get_cached_or_execute(
    const DAGNode& node,
    const std::vector<ArtifactPtr>& inputs
) {
    // Compute expected artifact ID
    auto expected_id = compute_expected_artifact_id(node, inputs);
    
    // Check cache
    if (cache_enabled_) {
        auto it = artifact_cache_.find(expected_id);
        if (it != artifact_cache_.end()) {
            ORC_LOG_TRACE("Node '{}': Using cached result (cache size: {})", node.node_id, artifact_cache_.size());
            return it->second;
        } else {
            ORC_LOG_TRACE("Node '{}': Cache miss - expected_id='{}' (cache size: {})", 
                         node.node_id, expected_id.value(), artifact_cache_.size());
        }
    }
    
    // Execute stage
    ORC_LOG_DEBUG("Node '{}': Executing stage '{}'", node.node_id, node.stage->get_node_type_info().stage_name);
    auto outputs = node.stage->execute(inputs, node.parameters);
    
    // Sink stages are allowed to return empty outputs (they consume inputs without producing outputs)
    bool is_sink = (node.stage->get_node_type_info().type == NodeType::SINK);
    
    if (outputs.empty() && !is_sink) {
        ORC_LOG_ERROR("Node '{}': Stage '{}' produced no outputs", node.node_id, node.stage->get_node_type_info().stage_name);
        throw DAGExecutionError("Stage '" + node.stage->get_node_type_info().stage_name + "' produced no outputs");
    }
    
    if (outputs.empty() && is_sink) {
        ORC_LOG_DEBUG("Node '{}': Sink stage executed (no outputs expected)", node.node_id);
        return nullptr;  // Sink stages don't produce artifacts
    }
    
    // Cache result using the expected_id (same key used for lookup)
    if (cache_enabled_) {
        ORC_LOG_TRACE("Node '{}': Caching result with expected_id='{}' (cache will be size: {})", 
                     node.node_id, expected_id.value(), artifact_cache_.size() + 1);
        artifact_cache_[expected_id] = outputs[0];
    }
    
    return outputs[0];
}

ArtifactID DAGExecutor::compute_expected_artifact_id(
    const DAGNode& node,
    const std::vector<ArtifactPtr>& inputs
) const {
    // Simple hash-based ID computation (placeholder)
    // In production, this would use proper content-addressing
    std::ostringstream oss;
    oss << node.stage->get_node_type_info().stage_name << ":" << node.stage->version();
    
    for (const auto& input : inputs) {
        if (!input) {
            throw DAGExecutionError("Null input artifact in compute_expected_artifact_id");
        }
        oss << ":" << input->id().value();
    }
    
    for (const auto& [key, value] : node.parameters) {
        oss << ":" << key << "=";
        // Append value based on variant type
        std::visit([&oss](const auto& v) { oss << v; }, value);
    }
    
    return ArtifactID(oss.str());
}

void DAGExecutor::clear_cache() {
    artifact_cache_.clear();
}

std::map<std::string, std::vector<ArtifactPtr>> DAGExecutor::execute_to_node(
    const DAG& dag,
    const std::string& target_node_id
) {
    ORC_LOG_DEBUG("Node '{}': Executing DAG to this node", target_node_id);
    
    if (!dag.validate()) {
        auto errors = dag.get_validation_errors();
        ORC_LOG_ERROR("DAG validation failed with {} errors", errors.size());
        std::ostringstream oss;
        oss << "DAG validation failed:\n";
        for (const auto& error : errors) {
            oss << "  - " << error << "\n";
        }
        throw DAGExecutionError(oss.str());
    }
    
    // Check that target node exists
    auto node_index = dag.build_node_index();
    if (node_index.find(target_node_id) == node_index.end()) {
        ORC_LOG_ERROR("Node '{}': Does not exist in DAG", target_node_id);
        throw DAGExecutionError("Target node '" + target_node_id + "' does not exist in DAG");
    }
    
    // Topological sort up to target node
    auto execution_order = topological_sort_to_node(dag, target_node_id);
    ORC_LOG_DEBUG("Node '{}': Execution order includes {} nodes", target_node_id, execution_order.size());
    
    // Execute nodes in order
    std::map<std::string, std::vector<ArtifactPtr>> node_outputs;
    
    // Initialize with root inputs (virtual node)
    node_outputs["__root__"] = dag.root_inputs();
    
    size_t total_nodes = execution_order.size();
    size_t current_node = 0;
    
    for (const auto& node_id : execution_order) {
        ++current_node;
        
        ORC_LOG_DEBUG("Node '{}': Executing ({}/{} in order)", node_id, current_node, total_nodes);
        
        if (progress_callback_) {
            progress_callback_(node_id, current_node, total_nodes);
        }
        
        const auto& node = dag.nodes()[node_index[node_id]];
        
        // Gather inputs
        std::vector<ArtifactPtr> inputs;
        
        if (node.input_node_ids.empty()) {
            // Root node with no dependencies (e.g., SOURCE nodes)
            // No inputs needed - stage generates output
            inputs = {};
        } else {
            for (size_t i = 0; i < node.input_node_ids.size(); ++i) {
                const auto& input_node_id = node.input_node_ids[i];
                size_t output_index = i < node.input_indices.size() ? node.input_indices[i] : 0;
                
                auto it = node_outputs.find(input_node_id);
                if (it == node_outputs.end() || output_index >= it->second.size()) {
                    throw DAGExecutionError("Missing input for node '" + node_id + "' from '" + input_node_id + "'");
                }
                
                inputs.push_back(it->second[output_index]);
            }
        }
        
        // Execute or retrieve from cache
        auto outputs = get_cached_or_execute(node, inputs);
        
        // Sink stages return nullptr (no outputs)
        if (outputs) {
            node_outputs[node_id] = {outputs};
        } else {
            node_outputs[node_id] = {};  // Empty vector for sink nodes
        }
    }
    
    return node_outputs;
}

std::vector<std::string> DAGExecutor::topological_sort_to_node(
    const DAG& dag,
    const std::string& target_node_id
) const {
    // Build dependency graph and find all nodes needed to compute target
    auto node_index = dag.build_node_index();
    std::set<std::string> required_nodes;
    
    // Recursive DFS to find all dependencies
    std::function<void(const std::string&)> collect_dependencies = 
        [&](const std::string& node_id) {
            if (required_nodes.find(node_id) != required_nodes.end()) {
                return;  // Already visited
            }
            
            required_nodes.insert(node_id);
            
            auto it = node_index.find(node_id);
            if (it != node_index.end()) {
                const auto& node = dag.nodes()[it->second];
                for (const auto& input_id : node.input_node_ids) {
                    collect_dependencies(input_id);
                }
            }
        };
    
    collect_dependencies(target_node_id);
    
    // Now do topological sort on only the required nodes
    std::map<std::string, size_t> in_degree;
    for (const auto& node_id : required_nodes) {
        in_degree[node_id] = 0;
    }
    
    for (const auto& node_id : required_nodes) {
        auto it = node_index.find(node_id);
        if (it != node_index.end()) {
            const auto& node = dag.nodes()[it->second];
            for (const auto& input_id : node.input_node_ids) {
                if (required_nodes.find(input_id) != required_nodes.end()) {
                    in_degree[input_id]++;
                }
            }
        }
    }
    
    // Kahn's algorithm on required nodes only
    std::queue<std::string> queue;
    for (const auto& [node_id, degree] : in_degree) {
        if (degree == 0) {
            queue.push(node_id);
        }
    }
    
    std::vector<std::string> result;
    while (!queue.empty()) {
        std::string node_id = queue.front();
        queue.pop();
        result.push_back(node_id);
        
        auto it = node_index.find(node_id);
        if (it != node_index.end()) {
            const auto& node = dag.nodes()[it->second];
            for (const auto& input_id : node.input_node_ids) {
                if (required_nodes.find(input_id) != required_nodes.end()) {
                    in_degree[input_id]--;
                    if (in_degree[input_id] == 0) {
                        queue.push(input_id);
                    }
                }
            }
        }
    }
    
    // Reverse for execution order (dependencies first)
    std::reverse(result.begin(), result.end());
    
    return result;
}

} // namespace orc
