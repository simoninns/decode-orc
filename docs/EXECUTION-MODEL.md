# Execution Model - Single Field vs Full Source

## Overview

The orc system has two distinct execution modes that serve different purposes. Understanding the distinction is critical to the architecture.

## Single Field Execution (Preview/Inspection)

**Purpose**: GUI preview, debugging, inspection at any DAG node

**How it works**:
```cpp
// User scrubs to field 42 and selects "Transform Node 1" in the DAG editor
auto result = renderer.render_field_at_node("transform_1", FieldID(42));
display_field(result.representation);
```

**What happens**:
1. GUI requests ONE specific field at ONE specific node
2. orc-core executes the DAG chain for ONLY that field
3. Processing stops at the requested node
4. Result is cached for fast scrubbing
5. No iteration through the source

**Key characteristics**:
- Fast and responsive (single field processing)
- On-demand rendering as user scrubs
- Can inspect at any node in the pipeline
- Used by `DAGFieldRenderer` class
- This is what the GUI uses

## Full Source Execution (Batch Processing)

**Purpose**: Final output to sink, complete processing of entire source

**How it works**:
```cpp
// User clicks "Process to Output"
for (auto field_id : source->field_range()) {
    auto outputs = executor.execute(dag);
    write_to_sink(outputs);
}
```

**What happens**:
1. User explicitly requests complete processing
2. orc-core iterates through ALL fields in source
3. Each field is processed through entire DAG
4. Output is written to sink node(s)
5. Progress bar shows completion

**Key characteristics**:
- Processes entire source (all fields)
- Outputs to sink (file, stream, etc.)
- Used for final deliverables
- Progress tracking needed
- NOT used for preview

## Architectural Rationale

### Why Single Field Execution?

**Responsiveness**: Processing one field is fast enough for interactive scrubbing
```
Single field: ~1-10ms depending on complexity
Full source: Minutes to hours for thousands of fields
```

**Simplicity**: No need for background workers, job queues, or progress management for preview

**Flexibility**: User can inspect ANY node at ANY field instantly

### Why Keep Them Separate?

**Different Requirements**:
- Preview needs interactivity
- Batch needs completeness and error handling

**Different Optimizations**:
- Preview uses caching for fast scrubbing
- Batch uses progress tracking and error recovery

**Clear Separation of Concerns**:
- `DAGFieldRenderer` - single field at any node
- `DAGExecutor` + iteration - full source to sink

## API Examples

### Preview Mode (Interactive)

```cpp
// Setup
DAGFieldRenderer renderer(dag);

// User scrubs to different fields
for (auto field_index : user_scrubbing) {
    FieldID field_id = source_range.start + field_index;
    auto result = renderer.render_field_at_node(current_node, field_id);
    
    if (result.is_valid) {
        update_display(result.representation);
        
        if (result.from_cache) {
            // Fast! Already processed
        }
    }
}
```

### Batch Mode (Non-Interactive)

```cpp
// Setup
DAGExecutor executor;
auto dag = build_dag();

// Process all fields
size_t total_fields = source->field_count();
for (size_t i = 0; i < total_fields; ++i) {
    FieldID field_id = source_range.start + i;
    
    // Execute entire DAG for this field
    auto outputs = executor.execute(dag);
    
    // Write to sink
    write_outputs_to_file(outputs);
    
    // Update progress
    update_progress_bar(i + 1, total_fields);
}
```

## Common Misconceptions

### ❌ "The renderer should process all fields for caching"

No. This would defeat the purpose. Single field execution means processing ONLY what's requested. If the user never scrubs to field 1000, it's never processed.

### ❌ "Preview and batch should use the same code path"

No. They have fundamentally different requirements:
- Preview: Interactive, one field at a time, any node
- Batch: Complete, all fields, to sink only

### ❌ "We need a job queue for preview"

No. Single field execution is fast enough for synchronous operation. Background processing adds complexity without benefit for preview.

## Design Benefits

1. **Simple mental model**: One field = one request
2. **Predictable performance**: Processing time proportional to field count requested
3. **Easy debugging**: Can inspect any field at any node
4. **Clear code paths**: Preview vs batch are separate concerns
5. **Efficient caching**: Only cache what's actually viewed

## Implementation Status

- ✅ Single field execution: Fully implemented in `DAGFieldRenderer`
- ✅ GUI integration: Partially implemented (signals connected, rendering stubbed)
- 🚧 Full source batch: Not yet implemented (planned for orc-process CLI)

## Related Documentation

- [DAG-FIELD-RENDERING.md](DAG-FIELD-RENDERING.md) - Complete API reference
- [DESIGN.md](DESIGN.md) - Overall architecture
- [GUI-DAG-INTEGRATION.md](GUI-DAG-INTEGRATION.md) - GUI integration details
