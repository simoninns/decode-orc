/******************************************************************************
 * dag_field_renderer.h
 *
 * DAG Field Renderer - Executes DAG up to a specific node to produce fields
 * for preview/inspection at any point in the processing pipeline
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#pragma once

#include "dag_executor.h"
#include "video_field_representation.h"
#include "field_id.h"
#include <memory>
#include <string>
#include <optional>
#include <stdexcept>

namespace orc {

/**
 * @brief Exception thrown during DAG field rendering
 */
class DAGFieldRenderError : public std::runtime_error {
public:
    explicit DAGFieldRenderError(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * @brief Result of rendering a field at a DAG node
 */
struct FieldRenderResult {
    /// The rendered field representation at the specified node
    VideoFieldRepresentationPtr representation;
    
    /// True if the result is valid and can be displayed
    bool is_valid;
    
    /// Error message if invalid (empty if valid)
    std::string error_message;
    
    /// The node that was rendered
    std::string node_id;
    
    /// The field that was requested
    FieldID field_id;
    
    /// True if this result came from cache
    bool from_cache;
};

/**
 * @brief Renders fields at any point in a DAG for preview/inspection
 * 
 * This class enables the GUI to:
 * 1. Request a specific field from any node in the DAG
 * 2. Have the pipeline execute only up to that node FOR THAT SINGLE FIELD
 * 3. Get invalidation notices when the DAG changes
 * 
 * Key principles:
 * - Field is the atomic unit of processing
 * - **Single field execution**: Executes DAG for one field at a time, not entire source
 * - **No full source iteration**: GUI requests fields on-demand (e.g., during scrubbing)
 * - **Batch processing is separate**: Full source execution (all fields to sink) is a 
 *   different operation, not handled by this class
 * - No "execute run" needed - fields are rendered on-demand
 * - All processing logic stays in orc-core
 * - DAG changes automatically invalidate cached results
 * 
 * Thread safety: Not thread-safe. Use from single thread only.
 */
class DAGFieldRenderer {
public:
    /**
     * @brief Construct a field renderer for a DAG
     * @param dag The DAG to render fields from
     */
    explicit DAGFieldRenderer(std::shared_ptr<const DAG> dag);
    
    ~DAGFieldRenderer() = default;
    
    // Prevent copying - renderer has state (cache, DAG reference) that shouldn't be duplicated
    DAGFieldRenderer(const DAGFieldRenderer&) = delete;
    DAGFieldRenderer& operator=(const DAGFieldRenderer&) = delete;
    
    // Allow moving for efficiency
    DAGFieldRenderer(DAGFieldRenderer&&) = default;
    DAGFieldRenderer& operator=(DAGFieldRenderer&&) = default;
    
    // ========================================================================
    // Field Rendering API
    // ========================================================================
    
    /**
     * @brief Render a specific field at a specific node
     * 
     * Executes the DAG from source(s) up to (and including) the specified node,
     * producing the field representation at that point in the pipeline.
     * 
     * **Single Field Execution**: This method processes ONLY the requested field,
     * not the entire source. The GUI calls this for each field as needed (e.g., 
     * when scrubbing). For batch processing (all fields to sink), use DAGExecutor 
     * directly in a separate operation.
     * 
     * @param node_id The node to render at (e.g., "SOURCE_0", "dropout_correct_1")
     * @param field_id The field to render
     * @return Result containing the field representation or error
     * 
     * @note Results are cached. Subsequent calls for the same node/field return cached data
     * @note Cache is automatically cleared when DAG is updated via update_dag()
     */
    FieldRenderResult render_field_at_node(
        const std::string& node_id,
        FieldID field_id
    );
    
    /**
     * @brief Check if a node exists in the DAG
     * @param node_id Node ID to check
     * @return True if node exists, false otherwise
     */
    bool has_node(const std::string& node_id) const;
    
    /**
     * @brief Get list of all node IDs that can be rendered
     * 
     * Returns all nodes in topological order (source to sink).
     * These are the valid node IDs that can be passed to render_field_at_node().
     * 
     * @return Vector of node IDs in execution order
     */
    std::vector<std::string> get_renderable_nodes() const;
    
    /**
     * @brief Get the DAG that this renderer is using
     * @return Shared pointer to the DAG
     */
    std::shared_ptr<const DAG> get_dag() const { return dag_; }
    
    // ========================================================================
    // DAG Change Tracking
    // ========================================================================
    
    /**
     * @brief Update the DAG used by this renderer
     * 
     * When the DAG structure or parameters change, call this method to:
     * 1. Update the internal DAG reference
     * 2. Clear all cached render results (they're now invalid)
     * 3. Invalidate any outstanding FieldRenderResults
     * 
     * @param new_dag The new DAG to use
     * 
     * @note This automatically invalidates all previous render results
     * @note Any FieldRenderResult objects from before this call should be discarded
     */
    void update_dag(std::shared_ptr<const DAG> new_dag);
    
    /**
     * @brief Get a monotonically increasing version number for the current DAG
     * 
     * This version increments every time update_dag() is called.
     * Clients can store this version and compare it later to detect if
     * the DAG has changed and their cached results are stale.
     * 
     * @return Current DAG version number
     */
    uint64_t get_dag_version() const { return dag_version_; }
    
    // ========================================================================
    // Cache Management
    // ========================================================================
    
    /**
     * @brief Clear all cached field render results
     * 
     * Useful for freeing memory when not actively rendering.
     * Does not increment DAG version.
     */
    void clear_cache();
    
    /**
     * @brief Get number of cached render results
     * @return Number of (node_id, field_id) combinations currently cached
     */
    size_t cache_size() const { return render_cache_.size(); }
    
    /**
     * @brief Enable or disable caching
     * @param enabled True to enable caching, false to disable
     * 
     * When disabled, every render_field_at_node() call will re-execute the DAG.
     * Useful for debugging or when memory is constrained.
     */
    void set_cache_enabled(bool enabled) { cache_enabled_ = enabled; }
    
    /**
     * @brief Check if caching is enabled
     * @return True if caching is enabled
     */
    bool is_cache_enabled() const { return cache_enabled_; }
    
private:
    /// The DAG being rendered
    std::shared_ptr<const DAG> dag_;
    
    /// DAG version number (increments on DAG updates)
    uint64_t dag_version_;
    
    /// Whether caching is enabled
    bool cache_enabled_;
    
    /// Cache key: (node_id, field_id, dag_version)
    struct CacheKey {
        std::string node_id;
        uint64_t field_id_value;
        uint64_t dag_version;
        
        bool operator<(const CacheKey& other) const {
            if (dag_version != other.dag_version) return dag_version < other.dag_version;
            if (node_id != other.node_id) return node_id < other.node_id;
            return field_id_value < other.field_id_value;
        }
    };
    
    /// Render result cache
    std::map<CacheKey, FieldRenderResult> render_cache_;
    
    /// Node index for fast lookup
    mutable std::map<std::string, size_t> node_index_;
    mutable bool node_index_valid_;
    
    /// Build/update node index
    void ensure_node_index() const;
    
    /// Execute DAG up to a specific node for a specific field
    FieldRenderResult execute_to_node(
        const std::string& node_id,
        FieldID field_id
    );
};

} // namespace orc
