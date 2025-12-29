/*
 * File:        dag_executor.h
 * Module:      orc-core
 * Purpose:     DAG execution engine
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include "artifact.h"
#include "node_id.h"
#include "../stages/stage.h"
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <functional>
#include <stdexcept>

namespace orc {

/**
 * @brief Exception thrown during DAG execution
 */
class DAGExecutionError : public std::runtime_error {
public:
    explicit DAGExecutionError(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * @brief Represents a node in the processing DAG
 */
struct DAGNode {
    NodeID node_id;              // Unique within DAG
    DAGStagePtr stage;                // Processing stage
    std::map<std::string, ParameterValue> parameters;  // Stage parameters (strong types)
    std::vector<NodeID> input_node_ids;        // Dependencies
    std::vector<size_t> input_indices;              // Which output from each input node
};

/**
 * @brief A complete processing DAG
 * 
 * The DAG is static and declarative. Execution order is derived from dependencies.
 * No dynamic mutation during execution.
 */
class DAG {
public:
    DAG() = default;
    
    // Prevent copying - DAG instances should be owned and referenced via shared_ptr
    DAG(const DAG&) = delete;
    DAG& operator=(const DAG&) = delete;
    
    // Allow moving for construction/initialization
    DAG(DAG&&) = default;
    DAG& operator=(DAG&&) = default;
    
    // DAG construction
    void add_node(DAGNode node);
    void set_root_inputs(const std::vector<ArtifactPtr>& inputs);
    void set_output_nodes(const std::vector<NodeID>& node_ids);
    
    // DAG validation
    bool validate() const;
    std::vector<std::string> get_validation_errors() const;
    
    // Accessors
    const std::vector<DAGNode>& nodes() const { return nodes_; }
    const std::vector<ArtifactPtr>& root_inputs() const { return root_inputs_; }
    const std::vector<NodeID>& output_nodes() const { return output_node_ids_; }
    
    // Helper for validation (needs to be public for DAGExecutor)
    std::map<NodeID, size_t> build_node_index() const;
    
private:
    std::vector<DAGNode> nodes_;
    std::vector<ArtifactPtr> root_inputs_;
    std::vector<NodeID> output_node_ids_;
    
    // Helper for validation
    bool has_cycle() const;
};

/**
 * @brief Executes a DAG, producing output artifacts
 * 
 * Handles:
 * - Topological sorting
 * - Caching (by artifact ID)
 * - Partial re-execution
 */
class DAGExecutor {
public:
    DAGExecutor() = default;
    
    // Prevent copying - executors have state (cache) that shouldn't be duplicated
    DAGExecutor(const DAGExecutor&) = delete;
    DAGExecutor& operator=(const DAGExecutor&) = delete;
    
    // Allow moving for efficiency
    DAGExecutor(DAGExecutor&&) = default;
    DAGExecutor& operator=(DAGExecutor&&) = default;
    
    // Execution
    std::vector<ArtifactPtr> execute(const DAG& dag);
    
    /**
     * @brief Execute DAG up to and including a specific node
     * 
     * Executes only the portion of the DAG needed to produce the output
     * of the specified node. Returns a map of all node outputs produced
     * during execution.
     * 
     * @param dag The DAG to execute
     * @param target_node_id The node to execute up to (inclusive)
     * @return Map of node_id -> list of output artifacts
     * @throws DAGExecutionError if target_node_id doesn't exist
     */
    std::map<NodeID, std::vector<ArtifactPtr>> execute_to_node(
        const DAG& dag,
        const NodeID& target_node_id
    );
    
    // Cache management
    void set_cache_enabled(bool enabled) { cache_enabled_ = enabled; }
    bool is_cache_enabled() const { return cache_enabled_; }
    
    void clear_cache();
    size_t cache_size() const { return artifact_cache_.size(); }
    
    // Progress callback (optional)
    using ProgressCallback = std::function<void(NodeID node_id, size_t current, size_t total)>;
    void set_progress_callback(ProgressCallback callback) { progress_callback_ = callback; }
    
private:
    bool cache_enabled_ = true;
    std::map<ArtifactID, ArtifactPtr> artifact_cache_;
    ProgressCallback progress_callback_;
    
    // Execution helpers
    std::vector<NodeID> topological_sort(const DAG& dag) const;
    std::vector<NodeID> topological_sort_to_node(
        const DAG& dag,
        const NodeID& target_node_id
    ) const;
    std::vector<ArtifactPtr> get_cached_or_execute(
        const DAGNode& node,
        const std::vector<ArtifactPtr>& inputs
    );
    ArtifactID compute_expected_artifact_id(
        const DAGNode& node,
        const std::vector<ArtifactPtr>& inputs
    ) const;
};

} // namespace orc
