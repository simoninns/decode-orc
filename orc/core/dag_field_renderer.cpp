/******************************************************************************
 * dag_field_renderer.cpp
 *
 * Implementation of DAG Field Renderer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "dag_field_renderer.h"
#include <sstream>
#include <algorithm>
#include <iostream>

namespace orc {

DAGFieldRenderer::DAGFieldRenderer(std::shared_ptr<const DAG> dag)
    : dag_(std::move(dag))
    , dag_version_(1)
    , cache_enabled_(true)
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
}

void DAGFieldRenderer::ensure_node_index() const
{
    if (!node_index_valid_) {
        node_index_ = dag_->build_node_index();
        node_index_valid_ = true;
    }
}

bool DAGFieldRenderer::has_node(const std::string& node_id) const
{
    ensure_node_index();
    return node_index_.find(node_id) != node_index_.end();
}

std::vector<std::string> DAGFieldRenderer::get_renderable_nodes() const
{
    // Return nodes in topological order (execution order)
    // This is useful for the GUI to present nodes in a logical sequence
    
    // We use DAGExecutor's topological sort
    // Note: This requires DAGExecutor to be enhanced or we implement our own
    // For now, just return node IDs in DAG order
    std::vector<std::string> result;
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
}

void DAGFieldRenderer::clear_cache()
{
    render_cache_.clear();
}

FieldRenderResult DAGFieldRenderer::render_field_at_node(
    const std::string& node_id,
    FieldID field_id)
{
    // Check node exists
    if (!has_node(node_id)) {
        std::cerr << "ERROR: Node '" << node_id << "' does not exist in DAG" << std::endl;
        FieldRenderResult error_result;
        error_result.is_valid = false;
        error_result.error_message = "Node '" + node_id + "' does not exist in DAG";
        error_result.node_id = node_id;
        error_result.field_id = field_id;
        error_result.from_cache = false;
        return error_result;
    }
    
    // Check cache
    if (cache_enabled_) {
        CacheKey key{node_id, field_id.value(), dag_version_};
        auto it = render_cache_.find(key);
        if (it != render_cache_.end()) {
            // Return cached result
            auto result = it->second;
            result.from_cache = true;
            return result;
        }
    }
    
    // Execute DAG to produce the field
    auto result = execute_to_node(node_id, field_id);
    
    // Cache the result
    if (cache_enabled_ && result.is_valid) {
        CacheKey key{node_id, field_id.value(), dag_version_};
        render_cache_[key] = result;
    }
    
    return result;
}

FieldRenderResult DAGFieldRenderer::execute_to_node(
    const std::string& node_id,
    FieldID field_id)
{
    FieldRenderResult result;
    result.node_id = node_id;
    result.field_id = field_id;
    result.from_cache = false;
    
    try {
        // Create a DAG executor
        DAGExecutor executor;
        executor.set_cache_enabled(true);  // Use executor's cache for efficiency
        
        // Execute the DAG up to the target node
        auto node_outputs = executor.execute_to_node(*dag_, node_id);
        
        // Get the output from our target node
        auto it = node_outputs.find(node_id);
        if (it == node_outputs.end() || it->second.empty()) {
            std::cerr << "ERROR: Node '" << node_id << "' produced no output" << std::endl;
            result.is_valid = false;
            result.error_message = "Node '" + node_id + "' produced no output";
            return result;
        }
        
        // The output should be a VideoFieldRepresentation
        auto video_field_repr = std::dynamic_pointer_cast<VideoFieldRepresentation>(it->second[0]);
        if (!video_field_repr) {
            std::cerr << "ERROR: Node '" << node_id << "' did not produce a VideoFieldRepresentation" << std::endl;
            result.is_valid = false;
            result.error_message = "Node '" + node_id + "' did not produce a VideoFieldRepresentation";
            return result;
        }
        
        // Verify the field exists in the representation
        if (!video_field_repr->has_field(field_id)) {
            std::cerr << "ERROR: Field " << field_id.to_string() << " not available in node '" << node_id << "'" << std::endl;
            result.is_valid = false;
            result.error_message = "Field " + field_id.to_string() + " not available in node '" + node_id + "'";
            return result;
        }
        
        result.is_valid = true;
        result.representation = video_field_repr;
        
        return result;
        
    } catch (const std::exception& e) {
        result.is_valid = false;
        result.error_message = std::string("Error rendering field: ") + e.what();
        std::cerr << "EXCEPTION in render_field_at_node: " << e.what() << std::endl;
        return result;
    }
}

} // namespace orc
