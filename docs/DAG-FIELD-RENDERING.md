# DAG Field Rendering - Design and Usage

## Overview

The DAG Field Renderer enables viewing video output at any point in the processing pipeline. This feature allows GUI users to inspect intermediate results and debug processing issues by viewing fields at different stages of the DAG.

## Key Principles

1. **Field as Atomic Unit**: All operations work at the field level - the fundamental unit of video processing
2. **Single Field Execution**: GUI requests a specific FieldID, and orc-core executes the DAG chain for ONLY that single field
3. **No Full Source Iteration**: The system does NOT iterate through all fields for preview - each field request is independent
4. **On-Demand Rendering**: No need to "execute a run" - fields are rendered when requested
5. **Core Logic Only**: All field handling logic is in `orc-core`, the GUI only requests and displays
6. **Automatic Invalidation**: When the DAG changes, all cached results are automatically invalidated
7. **Safety**: The system guards against showing stale data when the DAG is modified

### Single Field vs Full Source Execution

**For Preview (what DAGFieldRenderer does):**
- GUI requests one specific field at a specific node
- orc-core executes the DAG chain for ONLY that field
- Fast, responsive, on-demand rendering for scrubbing
- This keeps the structure simple

**For Final Output (separate batch operation):**
- User explicitly requests full processing to sink
- orc-core iterates through ALL available fields
- Produces complete output to sink node(s)
- This is NOT what DAGFieldRenderer is for

## Architecture

### Core Components

#### 1. `DAGFieldRenderer` (orc-core)
The main API for rendering fields at any DAG node.

**Key methods:**
```cpp
// Render a specific field at a specific node
FieldRenderResult render_field_at_node(
    const std::string& node_id,
    FieldID field_id
);

// Get list of all renderable nodes
std::vector<std::string> get_renderable_nodes() const;

// Update DAG (automatically invalidates cache)
void update_dag(std::shared_ptr<const DAG> new_dag);

// Track DAG changes
uint64_t get_dag_version() const;
```

#### 2. Enhanced `DAGExecutor`
Now supports partial execution up to a specific node:

```cpp
// Execute only up to target node
std::map<std::string, std::vector<ArtifactPtr>> execute_to_node(
    const DAG& dag,
    const std::string& target_node_id
);
```

### Data Flow

```
┌─────────────┐
│  GUI / CLI  │  Request field at node
└──────┬──────┘
       │
       ▼
┌────────────────────┐
│ DAGFieldRenderer   │  Check cache, execute if needed
└────────┬───────────┘
         │
         ▼
┌────────────────────┐
│   DAGExecutor      │  Execute pipeline up to node
└────────┬───────────┘
         │
         ▼
┌────────────────────┐
│ VideoFieldRepr     │  Return field representation
└────────────────────┘
```

## Usage Example

### Basic Usage

```cpp
#include "dag_field_renderer.h"

// Create or load a DAG
auto dag = std::make_shared<DAG>();
// ... add nodes ...

// Create renderer
DAGFieldRenderer renderer(dag);

// Get list of nodes that can be rendered
auto nodes = renderer.get_renderable_nodes();
for (const auto& node_id : nodes) {
    std::cout << "Can render: " << node_id << std::endl;
}

// Render a field at a specific node
FieldID field_to_view(100);
auto result = renderer.render_field_at_node("dropout_correct_1", field_to_view);

if (result.is_valid) {
    // Display the field
    auto field_data = result.representation->get_field(field_to_view);
    // ... render to screen ...
} else {
    std::cerr << "Error: " << result.error_message << std::endl;
}
```

### DAG Change Tracking

```cpp
// Store DAG version
uint64_t stored_version = renderer.get_dag_version();

// User modifies DAG
auto new_dag = modify_dag(old_dag);
renderer.update_dag(new_dag);

// Check if view is stale
if (renderer.get_dag_version() != stored_version) {
    // Previous view is invalid - need to re-render
    std::cout << "DAG changed - view invalidated" << std::endl;
}
```

### Caching

The renderer automatically caches field render results for performance:

```cpp
// First call executes the pipeline
auto result1 = renderer.render_field_at_node("node1", FieldID(0));
assert(!result1.from_cache);

// Second call for same node/field returns cached data
auto result2 = renderer.render_field_at_node("node1", FieldID(0));
assert(result2.from_cache);

// Updating DAG clears cache
renderer.update_dag(new_dag);

// Now it must execute again
auto result3 = renderer.render_field_at_node("node1", FieldID(0));
assert(!result3.from_cache);
```

## GUI Integration

The GUI should:

1. **Create a renderer when project loads**:
   ```cpp
   dag_renderer_ = std::make_unique<DAGFieldRenderer>(project_dag);
   ```

2. **Request fields when user selects a node**:
   ```cpp
   void on_node_selected(const std::string& node_id) {
       current_node_ = node_id;
       update_field_view();
   }
   
   void update_field_view() {
       auto result = dag_renderer_->render_field_at_node(
           current_node_, 
           current_field_
       );
       
       if (result.is_valid) {
           field_preview_widget_->setRepresentation(result.representation);
           field_preview_widget_->setFieldIndex(current_field_.value());
       } else {
           show_error(result.error_message);
       }
   }
   ```

3. **Update renderer when DAG changes**:
   ```cpp
   void on_dag_modified() {
       auto new_dag = build_dag_from_project();
       dag_renderer_->update_dag(new_dag);
       
       // Re-render current view
       update_field_view();
   }
   ```

4. **Provide node selection UI**:
   - Dropdown list or tree view of renderable nodes
   - Include source nodes (to view original data)
   - Include transform nodes (to view processed data)
   - Highlight which node is currently being viewed

## Example Workflow

### Viewing Dropout Correction Progress

1. User opens project with source → dropout_correct → output
2. GUI creates DAGFieldRenderer with the project's DAG
3. User selects field #100 in the field navigator
4. User selects "SOURCE_0" from node dropdown
   - Renderer executes up to SOURCE_0
   - Returns original field data
   - GUI displays field with dropouts visible
5. User selects "dropout_correct_1" from node dropdown
   - Renderer executes up to dropout_correct_1
   - Returns corrected field data
   - GUI displays same field after correction
6. User can toggle between nodes to compare results

### Handling DAG Changes

1. User is viewing field at "filter_1" node
2. User modifies filter parameters in DAG
3. GUI calls `renderer.update_dag(new_dag)`
4. Cache is automatically cleared
5. GUI re-renders current view with new parameters
6. User sees updated result immediately

## Performance Considerations

### Caching Strategy
- **Per-Field Caching**: Each (node_id, field_id, dag_version) is cached separately
- **Memory Management**: Cache size can be monitored via `cache_size()`
- **Manual Control**: Use `clear_cache()` to free memory when needed

### Optimization
- Only executes DAG up to target node (not the entire pipeline)
- Reuses DAGExecutor's internal artifact cache
- Subsequent field requests at same node are very fast

### When to Clear Cache
```cpp
// Clear when memory is constrained
if (renderer.cache_size() > 1000) {
    renderer.clear_cache();
}

// Disable caching for memory-critical situations
renderer.set_cache_enabled(false);
```

## Error Handling

The renderer provides detailed error information:

```cpp
auto result = renderer.render_field_at_node(node_id, field_id);

if (!result.is_valid) {
    // Common error cases:
    // 1. Node doesn't exist
    // 2. Field doesn't exist in source
    // 3. Execution error in pipeline
    // 4. Node doesn't produce VideoFieldRepresentation
    
    display_error_dialog(result.error_message);
}
```

## Testing

The feature includes comprehensive tests in `test_dag_field_renderer.cpp`:

- Basic rendering at different nodes
- Cache functionality
- DAG change tracking and invalidation
- Error handling (missing nodes, missing fields)

Run tests:
```bash
cd build/orc
ctest --output-on-failure
```

## Future Enhancements

Potential improvements:

1. **Observer Data Integration**: Include VBI/VITS observations in the field view
2. **Diff Mode**: Compare two nodes side-by-side
3. **Progressive Rendering**: Render multiple fields in background
4. **Export**: Save field at specific node to image file
5. **Performance Metrics**: Track rendering time per node

## Implementation Notes

### Why Field-Level?
Fields are the fundamental unit because:
- Most processing operates on individual fields
- Fields can be rendered independently
- Enables frame-accurate debugging
- Natural unit for VBI/VITS data

### Why No "Execute Run"?
Traditional video processing requires executing the entire pipeline before viewing results. This design inverts that - you view a specific field at a specific point on-demand. Benefits:
- Immediate feedback
- No waiting for full pipeline execution
- Natural debugging workflow
- Efficient memory usage (only compute what's needed)

### Thread Safety
`DAGFieldRenderer` is not thread-safe. For GUI usage, all calls should be from the UI thread or properly synchronized.
