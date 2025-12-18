# GUI DAG Integration

## Overview

This document describes the integration of DAG node selection and field rendering into the orc-gui main window.

## Implementation Status

### ✅ Completed

1. **MainWindow Enhancements**
   - Added `DAGFieldRenderer` member variable for future field rendering at DAG nodes
   - Added `current_view_node_id_` to track which node is currently being viewed
   - Added slot `onNodeSelectedForView()` to handle node selection from DAG editor
   - Added slot `onDAGModified()` to handle DAG changes
   - Added method `updateFieldView()` to update preview based on current view node
   - Added method `updateDAGRenderer()` to create/update the field renderer

2. **DAG Editor Connection**
   - Connected `DAGViewerWidget::nodeSelected` signal to `MainWindow::onNodeSelectedForView` slot
   - Connected `DAGEditorWindow::projectModified` signal to `MainWindow::onDAGModified` slot
   - When a node is clicked in the DAG editor, the main window is notified

3. **Status Bar Integration**
   - Status bar shows "Viewing output from node: [node_id]" when a node is selected
   - Status bar displays temporary messages for field rendering status

### 🚧 Pending Implementation

The following features have been stubbed but not fully implemented:

1. **Project to Execution DAG Conversion**
   - Need to convert `orc::Project` structure to `orc::DAG` execution graph
   - Requires mapping `ProjectDAGNode` to `DAGNode` with actual stage instances
   - Must handle stage instantiation based on stage_name and parameters

2. **Field Rendering at DAG Nodes**
   - Currently `updateFieldView()` renders from the original source representation
   - Should use `DAGFieldRenderer::render_field_at_node()` to render at selected node
   - Requires working Project-to-DAG conversion

3. **Cache Management**
   - DAG changes should invalidate renderer cache via `DAGFieldRenderer::update_dag()`
   - Field cache should persist across node selections for performance

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ MainWindow                                                      │
│  ┌──────────────────┐    ┌────────────────────────────────┐   │
│  │ FieldPreviewWidget│◄───│ DAGFieldRenderer (future)      │   │
│  │                   │    │                                 │   │
│  │ Shows current     │    │ render_field_at_node()         │   │
│  │ field at selected │    │ - Executes DAG up to node      │   │
│  │ node              │    │ - Returns field representation │   │
│  └──────────────────┘    └────────────────────────────────┘   │
│         ▲                            ▲                          │
│         │                            │                          │
│         │        onNodeSelectedForView(node_id)                │
│         │                            │                          │
│         └────────────────────────────┘                          │
└─────────────────────────────────────────────────────────────────┘
                    ▲
                    │ nodeSelected(node_id)
                    │
┌─────────────────────────────────────────────────────────────────┐
│ DAGEditorWindow                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ DAGViewerWidget                                          │  │
│  │  - User clicks on node                                   │  │
│  │  - Emits nodeSelected(node_id) signal                    │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## User Workflow

1. User opens a project with a source TBC file
2. User clicks "Edit DAG" to open the DAG editor window
3. User clicks on a node in the DAG graph
4. **Current behavior:**
   - Status bar shows "Viewing output from node: [node_id]"
   - Preview still shows original source field (not yet transformed)
5. **Future behavior (when Project-to-DAG conversion is implemented):**
   - Preview updates to show the field as it appears at that node
   - **Single Field Execution**: Only the current field is processed through the DAG
   - **No Full Source Processing**: The system does NOT iterate through all fields
   - All transforms up to the selected node are applied
   - Result is cached for fast scrubbing through fields
   - When user wants full output, that's a separate batch operation

## Signal Flow

```
User clicks node in DAGViewerWidget
    ↓
DAGViewerWidget::nodeSelected(node_id) signal emitted
    ↓
MainWindow::onNodeSelectedForView(node_id) slot called
    ↓
- current_view_node_id_ updated
- Status bar message displayed
- updateFieldView() called
    ↓
updateFieldView() updates preview
    ↓
FieldPreviewWidget displays field
```

## Code References

### Key Files Modified

1. **[orc/gui/mainwindow.h](../orc/gui/mainwindow.h)**
   - Added forward declaration for `DAGFieldRenderer`
   - Added member `field_renderer_` (unique_ptr)
   - Added member `current_view_node_id_` (string)
   - Added slots: `onNodeSelectedForView()`, `onDAGModified()`
   - Added methods: `updateFieldView()`, `updateDAGRenderer()`

2. **[orc/gui/mainwindow.cpp](../orc/gui/mainwindow.cpp)**
   - Implemented new slots and methods
   - Connected DAG editor signals in `onOpenDAGEditor()`
   - Added `updateDAGRenderer()` call in `openProject()`

3. **Core Library**
   - See [DAG-FIELD-RENDERING.md](DAG-FIELD-RENDERING.md) for details on the core rendering API

### Signal Connections

In `MainWindow::onOpenDAGEditor()`:

```cpp
// Connect node selection signal for field rendering
connect(dag_editor_window_->dagViewer(), &DAGViewerWidget::nodeSelected,
        this, &MainWindow::onNodeSelectedForView);

// Connect DAG modification signal to update renderer
connect(dag_editor_window_, &DAGEditorWindow::projectModified,
        this, &MainWindow::onDAGModified);
```

## Next Steps

To complete the feature, the following work is required:

1. **Implement Project-to-DAG Conversion**
   - Create a function that converts `orc::Project` to `orc::DAG`
   - Instantiate actual stage objects based on stage_name
   - Handle stage parameters from ProjectDAGNode
   - Consider creating a stage registry/factory

2. **Complete Field Rendering Integration**
   - Update `updateFieldView()` to use `field_renderer_->render_field_at_node()`
   - Handle error cases (invalid node, field not available, etc.)
   - Display cache hit status to user

3. **Add Unit Tests**
   - Test signal connections
   - Test node selection updates
   - Test field rendering at different nodes

4. **Performance Optimization**
   - Profile field rendering performance
   - Consider background rendering for upcoming fields
   - Implement progressive rendering for large fields

## Testing

### Build Verification

```bash
cd build
make orc-gui -j$(nproc)
```

### Test Suite

All existing tests pass:

```bash
cd build/orc
ctest --output-on-failure
```

Output:
```
100% tests passed, 0 tests failed out of 7
```

### Manual GUI Testing

1. Launch `./build/bin/orc-gui`
2. Open a project with a source
3. Click "Edit DAG" menu item
4. Click on a node in the DAG viewer
5. Verify status bar shows "Viewing output from node: [node_id]"

## Related Documentation

- [DAG-FIELD-RENDERING.md](DAG-FIELD-RENDERING.md) - Core field rendering API
- [DESIGN.md](DESIGN.md) - Overall system architecture
- [GUI-DESIGN.md](GUI-DESIGN.md) - GUI architecture
