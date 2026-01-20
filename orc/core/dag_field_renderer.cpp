/*
 * File:        dag_field_renderer.cpp
 * Module:      orc-core
 * Purpose:     Field rendering at DAG nodes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "dag_field_renderer.h"
#include "observation_wrapper_representation.h"
// TODO: Observer system refactored - old observers removed
#include "logging.h"
#include <sstream>
#include <algorithm>

namespace orc {

DAGFieldRenderer::DAGFieldRenderer(std::shared_ptr<const DAG> dag)
    : dag_(std::move(dag))
    , dag_version_(1)
    , cache_enabled_(true)
    , render_cache_(MAX_CACHED_RENDERS)
    , node_index_valid_(false)
{
    if (!dag_) {
        throw DAGFieldRenderError("Cannot create renderer with null DAG");
    }
    
    if (!dag_->validate()) {
        auto errors = dag_->get_validation_errors();
        std::ostringstream oss;
        oss << "Cannot create renderer with invalid DAG:\n";
        for (const auto& error : errors) {
            oss << "  - " << error << "\n";
        }
        throw DAGFieldRenderError(oss.str());
    }
    
    // Create persistent executor with caching enabled
    executor_ = std::make_unique<DAGExecutor>();
    executor_->set_cache_enabled(true);
}

void DAGFieldRenderer::ensure_node_index() const
{
    if (!node_index_valid_) {
        node_index_ = dag_->build_node_index();
        node_index_valid_ = true;
    }
}

bool DAGFieldRenderer::has_node(NodeID node_id) const
{
    ensure_node_index();
    return node_index_.find(node_id) != node_index_.end();
}

std::vector<NodeID> DAGFieldRenderer::get_renderable_nodes() const
{
    // Return nodes in topological order (execution order)
    // This is useful for the GUI to present nodes in a logical sequence
    
    // We use DAGExecutor's topological sort
    // Note: This requires DAGExecutor to be enhanced or we implement our own
    // For now, just return node IDs in DAG order
    std::vector<NodeID> result;
    for (const auto& node : dag_->nodes()) {
        result.push_back(node.node_id);
    }
    return result;
}

void DAGFieldRenderer::update_dag(std::shared_ptr<const DAG> new_dag)
{
    if (!new_dag) {
        throw DAGFieldRenderError("Cannot update to null DAG");
    }
    
    if (!new_dag->validate()) {
        auto errors = new_dag->get_validation_errors();
        std::ostringstream oss;
        oss << "Cannot update to invalid DAG:\n";
        for (const auto& error : errors) {
            oss << "  - " << error << "\n";
        }
        throw DAGFieldRenderError(oss.str());
    }
    
    dag_ = std::move(new_dag);
    ++dag_version_;
    node_index_valid_ = false;
    
    // Clear cache - all previous results are now invalid
    render_cache_.clear();
    
    // Reset executor with fresh cache for new DAG
    executor_ = std::make_unique<DAGExecutor>();
    executor_->set_cache_enabled(true);
}

void DAGFieldRenderer::clear_cache()
{
    render_cache_.clear();
}

FieldRenderResult DAGFieldRenderer::render_field_at_node(
    NodeID node_id,
    FieldID field_id)
{
    ORC_LOG_TRACE("Node '{}': render_field_at_node, field {}", node_id.to_string(), field_id.value());
    
    // Check node exists
    if (!has_node(node_id)) {
        ORC_LOG_ERROR("Node '{}': Does not exist", node_id.to_string());
        FieldRenderResult error_result;
        error_result.is_valid = false;
        error_result.error_message = "Node '" + node_id.to_string() + "' does not exist in DAG";
        error_result.node_id = node_id;
        error_result.field_id = field_id;
        error_result.from_cache = false;
        return error_result;
    }
    
    // Check cache
    if (cache_enabled_) {
        CacheKey key{node_id, field_id.value(), dag_version_};
        auto cached_result = render_cache_.get(key);
        if (cached_result.has_value()) {
            // Return cached result
            ORC_LOG_TRACE("Node '{}': Returning cached result for field {}", node_id, field_id.value());
            cached_result->from_cache = true;
            return *cached_result;
        }
        ORC_LOG_DEBUG("Node '{}': Cache miss, will execute DAG", node_id.to_string());
    }
    
    // Execute DAG to produce the field
    auto result = execute_to_node(node_id, field_id);
    
    // Cache the result
    if (cache_enabled_ && result.is_valid) {
        CacheKey key{node_id, field_id.value(), dag_version_};
        render_cache_.put(key, result);
    }
    
    return result;
}

FieldRenderResult DAGFieldRenderer::execute_to_node(
    NodeID node_id,
    FieldID field_id)
{
    FieldRenderResult result;
    result.node_id = node_id;
    result.field_id = field_id;
    result.from_cache = false;
    
    ORC_LOG_DEBUG("Node '{}': Executing DAG for field {}", node_id, field_id.value());
    
    try {
        // Use the persistent executor (maintains cache across field requests)
        // This is critical for performance - batch stages execute once for all fields
        auto node_outputs = executor_->execute_to_node(*dag_, node_id);
        
        // Get the output from our target node
        auto it = node_outputs.find(node_id);
        if (it == node_outputs.end() || it->second.empty()) {
            ORC_LOG_ERROR("Node '{}': Produced no output", node_id);
            result.is_valid = false;
            result.error_message = fmt::format("Node '{}' produced no output", node_id);
            return result;
        }
        
        // The output should be a VideoFieldRepresentation
        auto video_field_repr = std::dynamic_pointer_cast<VideoFieldRepresentation>(it->second[0]);
        if (!video_field_repr) {
            ORC_LOG_ERROR("Node '{}': Did not produce a VideoFieldRepresentation", node_id.to_string());
            result.is_valid = false;
            result.error_message = "Node '" + node_id.to_string() + "' did not produce a VideoFieldRepresentation";
            return result;
        }
        
        // Verify the field exists in the representation
        if (!video_field_repr->has_field(field_id)) {
            ORC_LOG_WARN("Node '{}': Field {} not available", node_id.to_string(), field_id.to_string());
            result.is_valid = false;
            result.error_message = "Field " + field_id.to_string() + " not available in node '" + node_id.to_string() + "'";
            return result;
        }
        
        result.is_valid = true;
        result.representation = video_field_repr;
        
        // Run observers on the field and attach observations
        result.representation = attach_observations(video_field_repr, field_id);
        
        ORC_LOG_DEBUG("Node '{}': Field {} rendered successfully with observations", 
                     node_id.to_string(), field_id.to_string());
        
        return result;
        
    } catch (const std::exception& e) {
        result.is_valid = false;
        result.error_message = std::string("Error rendering field: ") + e.what();
        ORC_LOG_ERROR("Node '{}': Exception rendering field {}: {}", 
                     node_id.to_string(), field_id.to_string(), e.what());
        return result;
    }
}

std::shared_ptr<VideoFieldRepresentation> DAGFieldRenderer::attach_observations(
    std::shared_ptr<VideoFieldRepresentation> representation,
    FieldID field_id)
{
    // TODO: Observer system refactored - observers now run within stages via ObservationContext
    // This function is deprecated and will be removed once migration is complete
    // For now, return empty observation wrapper
    std::map<FieldID, std::vector<std::shared_ptr<Observation>>> obs_map;
    obs_map[field_id] = {};
    
    return std::make_shared<ObservationWrapperRepresentation>(
        std::move(representation),
        std::move(obs_map)
    );
}

} // namespace orc
