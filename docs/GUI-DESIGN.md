# orc-gui - Design and Implementation Status

## Implementation Status (December 2025)

### ✅ Completed Features

**Core GUI Framework**
- Qt6-based application with QMainWindow
- Project-based workflow: all work happens within a project context
- Project files (.orc-project) are YAML files managed by orc-core
- Projects contain one or more TBC sources (currently single source supported)
- Projects contain the DAG that defines how to process the sources
- Command-line argument support for quick TBC loading (creates temporary project)
- Menu structure: File (New Project, Open Project, Save Project, Open TBC Quick), Tools (DAG Editor)
- Field/frame preview with mode-aware navigation (moves by 1 field in field view, 2 fields in frame view)

**Interactive DAG Editor**
- QGraphicsView-based DAG visualization
- Visual node representation with connection point indicators
- Node rendering adapts to node type (SOURCE: 0 inputs; TRANSFORM: 1 in/1 out; SPLITTER: 1 in/multiple out; MERGER: multiple in/1 out; COMPLEX: multiple in/multiple out)
- Single input port on left edge (simple circle for single input, concentric circles for multiple inputs)
- Single output port on right edge (simple circle for single output, concentric circles for multiple outputs)
- Connection point visualization: ○ = single connection, ⊙ = multiple connections supported
- Drag-to-connect edge creation (drag from output → input)
- Connection validation enforces node type constraints (prevents connecting to full ports)
- Node movement and repositioning
- Source node for each source in the project (SOURCE type: no inputs, one output)
- Source nodes display source name from orc-core (e.g., "Source: video.tbc")
- Source nodes positioned on left side (-450 + offset_y) for natural left-to-right flow
- Selectable nodes and edges (edges highlighted when selected)
- DAG editor available when project has at least one source
- DAG editor automatically closes when last source is removed
- DAG is part of the project and saved/loaded with the project file
- Node rendering driven by orc-core node type metadata (NodeType enum: SOURCE, SINK, TRANSFORM, SPLITTER, MERGER, COMPLEX)
- Connection validation enforced by orc-core (e.g., can't connect to SOURCE node input)
- Edge creation prevented when target port already has maximum connections

**Node Management**
- Add nodes via context menu or keyboard (only user-creatable types shown)
- Delete nodes via context menu or Del/Backspace key (Source nodes cannot be deleted directly)
- Change node type (stage) via context menu
- Nodes deleted automatically remove connected edges
- Node display names provided by orc-core (unique node_id + display_name architecture)

**Edge Management**
- Edges are selectable graphics items
- Delete edges via context menu "Delete Edge" or Del/Backspace key
- Curved Bezier edges with arrow heads
- Visual feedback (thicker, blue highlight) when selected

**Parameter System**
- `ParameterizedStage` interface for stages to expose parameters
- `ParameterValue` variant supporting int32, uint32, double, bool, string
- `ParameterDescriptor` with type, constraints, display name, description
- `StageParameterDialog` auto-generates UI from descriptors:
  - QSpinBox for integers
  - QDoubleSpinBox for floats
  - QCheckBox for booleans
  - QComboBox for string choices
- Parameter persistence: values stored in DAGNodeItem and survive editing
- "Edit Parameters" grayed out for stages without parameters

**DAG Serialization**
- YAML format with `nodes` (id, stage, x/y position, parameters) and `edges`
- `dag_serialization::load_dag_from_yaml()` - parse YAML into GUIDAG
- `dag_serialization::save_dag_to_yaml()` - write formatted YAML
- Menu shortcuts: Ctrl+L (Load), Ctrl+S (Save)
- 7 example DAG files in dag-examples/ folder

**Project System**
- Project files (.orc-project) are YAML format
- Project file structure:
  - Project metadata (name, version)
  - Sources list (source_id, path, display_name)
  - DAG nodes (including Source nodes with source_id reference, with x/y positions)
  - DAG edges
- Project file I/O handled by orc-core (orc::project_io namespace)
- Shared format between orc-gui and orc-process (CLI tool)
- GUIProject class wraps orc::Project and manages TBC representation loading
- Project name derived from filename (e.g., "my_project.orc-project" → name: "my_project")
- Supports multiple sources with automatic source ID management
- Complete CRUD API in orc-core for all DAG operations (add_node, remove_node, add_edge, remove_edge, set_node_parameters, set_node_position, change_node_type)
- Modification tracking entirely in orc-core (is_modified flag, automatically set by all CRUD operations, cleared on save/load)
- Modified indicator (*) appears in both MainWindow and DAG Editor window titles when project has unsaved changes
- Signal chain: DAG changes → dagModified signal → projectModified signal → updateUIState → updateWindowTitle
- Clear project functionality (clear_project) resets all project data
- Node positions persist in project file and are restored when loading
- DAG editor window automatically closes when all sources are removed

**Error Handling**
- Methods return bool with optional error strings
- QMessageBox warnings for failed operations
- Null pointer validation throughout
- Try-catch blocks for exception safety

**Architecture**
- Complete separation of concerns: GUI handles presentation and input, core handles all business logic
- No business logic in GUI layer (no validation, no data manipulation beyond calling core APIs)
- All DAG operations go through orc-core CRUD API (project_io namespace)
- Modification tracking managed entirely by core (mutable is_modified flag)
- GUI queries core for validation decisions (e.g., can_change_node_type)
- Signal-based communication for UI updates (dagModified → projectModified → updateUIState)
- GUIProject is a thin Qt wrapper providing QString conversion and TBC representation loading

### 🚧 Known Issues

**Memory Management (Critical)**
- Segfaults occur in some deletion scenarios
- Race conditions between Qt's scene management and manual deletion
- Edge invalidation and cleanup needs refinement
- `QApplication::processEvents()` added but not fully resolving issues
- Needs comprehensive review and possible refactoring to use Qt's object ownership

**Missing Features**
- Field/frame preview not yet wired to TBC loading
- DAG execution not implemented (visual only)
- No undo/redo for DAG editing
- No zoom/pan controls for DAG canvas
- No multi-selection with rubber band
- No grid snapping for node positioning

### 🔮 Planned Features

**DAG Execution Integration**
- Wire DAG nodes to actual video processing
- Show node execution progress and state
- Preview selected node's output in preview pane
- Real-time field processing visualization

**Enhanced UX**
- Undo/redo command pattern
- Grid snapping and alignment tools
- Node groups and collapsing
- Zoom and pan controls
- Rubber band multi-selection
- Keyboard shortcuts for common operations

**Multi-source Support**
- Multiple Source nodes for different sources
- Source alignment visualization
- Field fingerprinting UI

**Advanced Parameters**
- Parameter groups and categories
- Conditional parameters (visibility depends on other values)
- Parameter validation with real-time feedback
- Parameter presets and templates

---

## Purpose

This document defines the GUI design for **decode-orc**, implemented in **Qt6 / C++**. It consolidates all GUI-related design decisions into a single reference suitable for beginning implementation.

The GUI is designed to:
* Support complex, long-running decode workflows
* Treat input media as first-class, multi-source entities
* Make field-level inspection, alignment, and provenance visible
* Scale cleanly toward stacking and advanced analysis

---

## 1. High-Level GUI Goals

* Visualize and control a **DAG-based processing pipeline**
* Manage **multiple related TBC sources** per project
* Enable **fast field/frame navigation and inspection**
* Surface **alignment and provenance**, not hide them
* Remain responsive during long-running processing
* Keep GUI logic decoupled from core processing logic

---

## 2. Architectural Pattern

### Pattern

**MVC using Qt idioms** (Qt Models + Views + Controllers)

Qt is used only in the GUI layer. Core processing, DAG logic, alignment, and stacking must remain Qt-independent.

### Layering

```
GUI (Qt Widgets / Graphics)
│
├─ View
│   ├─ DAG viewer
│   ├─ Source manager
│   ├─ Field / frame preview
│   └─ Timelines & inspectors
│
├─ Controller
│   ├─ User actions → commands
│   ├─ Validation & orchestration
│
└─ Qt Models (adapters)
    ├─ PipelineModel
    ├─ SourceSetModel
    ├─ FieldIndexModel
    └─ JobModel
```

---

## 3. Main Window Layout

```
+---------------------------------------------------+
| Menu / Toolbar (project, source selection)        |
+----------------------+----------------------------+
| Project / Sources   |  Main Workspace             |
| Explorer             |  (DAG / Preview / Timeline) |
+----------------------+----------------------------+
| Job Status / Logs / Progress                      |
+---------------------------------------------------+
```

---

## 4. Input Media Handling

### 4.1 Core Concept: SourceSet

A **SourceSet** represents a logical group of related captures originating from the same master content.

Examples:
* Multiple captures of the same disc
* Multiple discs from the same master
* Re-captures with different TBC settings

Files are not treated as standalone inputs once imported.

### 4.2 Core Data Model (Non-Qt)

#### SourceSet
```
SourceSet
├─ uuid
├─ display_name
├─ master_fingerprint (optional)
├─ sources[]
```

#### Source
```
Source
├─ uuid
├─ path
├─ capture_metadata
│   ├─ tbc_type
│   ├─ timing_standard
│   ├─ field_rate
│   ├─ capture_date
├─ fingerprint
├─ alignment_map (optional)
└─ status
```

---

## 5. Sources Manager UI

### 5.1 Sources Panel

* Implemented with `QTreeView`
* Backed by `SourceSetModel`

```
Sources
├─ Movie XYZ (PAL)
│  ├─ Capture A (TBC1)
│  ├─ Capture B (TBC2)
│  └─ Capture C (damaged)
```

### 5.2 Import Workflow

* File → Import TBC Sources
* Drag-and-drop support

Import dialog:
* Select one or more TBC files
* Assign to:
  * New SourceSet
  * Existing SourceSet
* Optional fingerprinting on import

---

## 6. Active Source Selection

### 6.1 Global Active Selection

At all times:
* One **active SourceSet**
* One **active Source** within that set

UI placement:
* Toolbar dropdowns

```
[ SourceSet ▼ ] [ Source ▼ ]
```

### 6.2 Behavior

* Switching source updates preview instantly
* Logical field position is preserved
* No DAG rebuild unless explicitly required

---

## 7. DAG Viewer

### 7.1 Purpose

* Visualize generated DAGs
* Show node status and data flow
* Provide inspection, not authoring (initially)

### 7.2 Implementation

* `QGraphicsView` / `QGraphicsScene`
* Custom node and edge items

Node states:
* Pending
* Running
* Completed
* Failed

---

## 8. Field / Frame Preview System

### 8.1 Core Principle

Preview is a **rendering mode over logical fields**, independent of source and pipeline state.

### 8.2 Preview Modes

```
enum class PreviewMode {
    SingleField,
    Frame_EvenOdd,
    Frame_OddEven,
    Fields_Vertical
};
```

Supported views:
* One field at a time
* Two fields woven into a frame (EO / OE)
* Two fields stacked vertically

---

## 9. Preview Widget Architecture

### 9.1 Main Widget

```
FieldPreviewWidget
├─ RenderSurface (QOpenGLWidget recommended)
├─ PreviewController
└─ InputHandler
```

### 9.2 Rendering Notes

* QOpenGLWidget preferred for scrubbing performance
* CPU rendering via QImage acceptable initially
* Compositing logic is stateless and reusable

---

## 10. Logical Field Cursor

All preview modes share a single cursor:

```
logical_field_index
```

Derived fields:
* Field A = index
* Field B = index ± 1

Guarantees:
* Mode switching does not change position
* Source switching stays aligned

---

## 11. Navigation & Scrubbing

### 11.1 Keyboard Navigation

| Action | Effect |
|------|-------|
| Left / Right | ±1 field |
| Shift + Left / Right | ±1 frame |
| Page Up / Down | Large step |
| Home / End | Start / end |

### 11.2 Scrubber

* Slider represents logical field index range
* Non-blocking updates
* Decoupled from decode latency

---

## 12. Performance & Responsiveness

### 12.1 Asynchronous Fetch

* GUI thread never blocks on decode
* Worker threads fetch fields
* GUI updates on signal

### 12.2 Caching

* Ring buffer of decoded fields
* Temporary composited frame cache
* Graceful fallback when data is missing

---

## 13. Multi-Source Preview (Design-Ready)

Even before stacking is implemented:

* Multiple preview panes bound to same field index
* Same preview mode across sources
* Independent render surfaces

Used for:
* Visual comparison
* Dropout detection
* Alignment validation

---

## 14. Timeline & Alignment Visualization

### Timeline

* Represents logical field indices
* Physical sources map via alignment maps

### Alignment Indicators (Future)

* Offset overlays
* Confidence markers
* Missing / duplicated regions

---

## 15. Provenance Inspection

For any output or node:

* Right-click → Show Provenance
* Tree or graph view of:
  * Input sources
  * Alignment hints used
  * Fingerprints matched
  * Processing nodes applied

---

## 16. Integration with the DAG

### Input Nodes

The DAG consumes a **SourceSet**, not a file path.

```
InputNode
├─ source_set_uuid
├─ selection_policy
│   ├─ single-source
│   ├─ stacked (future)
│   └─ fallback order
```

This allows source swapping without pipeline mutation.

---

## 17. Threading Model

* Core processing runs outside GUI thread
* Communication via signals/slots or message queues
* GUI can attach to running jobs and recover state

---

## 18. Project Integration

### 18.1 Project Files

The GUI uses the project file system defined in the core architecture (see `DESIGN.md` Section 8).

**Key points**:

- Project files (`.orc-project`) are YAML format managed by `orc-core`
- Format is shared between `orc-gui` and `orc-process`
- Projects contain sources list and DAG structure
- Source nodes in DAG reference sources by `source_id`

### 18.2 GUIProject Wrapper

The GUI wraps `orc::Project` with `GUIProject` class:

```cpp
class GUIProject {
    // Project operations
    bool newEmptyProject(const QString& project_name, QString* error = nullptr);
    bool addSource(const QString& tbc_path, QString* error = nullptr);
    bool removeSource(int source_id, QString* error = nullptr);
    bool saveToFile(const QString& path, QString* error = nullptr);
    bool loadFromFile(const QString& path, QString* error = nullptr);
    void clear();
    
    // Source access (supports multiple sources)
    bool hasSource() const;
    int getSourceId() const;  // Returns first source ID, -1 if none
    QString getSourcePath() const;  // Returns first source path
    QString getSourceName() const;  // Returns first source name
    std::shared_ptr<const orc::VideoFieldRepresentation> getSourceRepresentation() const;
    
    // Core project access
    orc::Project& coreProject();
    
    // Modification tracking (delegates to core)
    bool isModified() const { return core_project_.has_unsaved_changes(); }
};
```

**Responsibilities**:

- Qt-friendly interface (QString instead of std::string)
- Manages TBC representation loading for preview
- Provides convenient accessors for GUI components
- Delegates all project operations to orc-core (add/remove sources, save/load, clear)
- Delegates source lifecycle to orc-core (add/remove with automatic Source node creation)
- Prevents duplicate source paths (enforced by core)
- Modification tracking entirely delegated to core (no GUI-side modified flag)
- Pure wrapper with no business logic

### 18.3 Workflow

1. **New Project**: User creates empty project (File → New Project)
   - File dialog to specify project location and filename
   - Project name automatically derived from filename
   - Project immediately saved to disk as empty .orc-project file
   - Enables "Add Source" and "Save Project" menu actions

2. **Add Source**: User adds TBC file(s) to project (File → Add Source)
   - File dialog to select TBC file
   - `GUIProject::addSource()` calls `orc::project_io::add_source_to_project()`
   - Core adds source to project with auto-generated source ID
   - Core creates Source node for the source in DAG (positioned vertically for multiple sources)
   - Core checks for duplicate paths (prevents adding same TBC twice)
   - GUI loads TBC representation for preview
   - Marks project as modified

3. **Remove Source**: User removes a source (File → Remove Source)
   - Confirmation dialog prompts user
   - `GUIProject::removeSource()` calls `orc::project_io::remove_source_from_project()`
   - Core removes source and Source node
   - Core removes all edges connected to that Source node
   - Core clears entire DAG if no sources remain
   - GUI closes DAG editor if open
   - Core marks project as modified (is_modified flag set automatically)

4. **Edit DAG**: User opens DAG editor (Tools → DAG Editor)
   - Only enabled when project has at least one source
   - DAG editor receives project reference via `setProject()`
   - Loads project's DAG nodes and edges into visual editor
   - User adds processing nodes, changes node types, sets parameters, and connects them
   - All operations go through orc-core CRUD API:
     - `add_node()` - Create new node at position
     - `remove_node()` - Delete node and connected edges
     - `change_node_type()` - Change node's stage type (validated by core)
     - `set_node_parameters()` - Update node parameters
     - `set_node_position()` - Update node x/y position
     - `add_edge()` - Connect two nodes
     - `remove_edge()` - Disconnect edge
   - Each operation automatically marks project as modified in core
   - DAG changes emit dagModified signal → projectModified signal → updateUIState
   - Window titles update immediately to show modified state (*)
   - Node positions persist and are saved with project

5. **Save Project**: User saves project file (File → Save Project)
   - Saves to current file location (Ctrl+S)
   - `orc::project_io::save_project()` serializes entire project (sources + DAG + positions) to `.orc-project` YAML file
   - Core clears modified flag automatically after successful save
   - Window titles update to remove asterisk (*)

6. **Save As**: User saves project to new location (File → Save Project As)
   - Prompts for new file location
   - Updates project path
   - Saves and core clears modified flag
   - Window titles update to remove asterisk (*)

7. **Open Project**: User opens existing project (File → Open Project)
   - File dialog to select .orc-project file
   - `orc::project_io::load_project()` loads project from YAML file
   - Core clears modified flag after successful load
   - GUI closes any open DAG editor window first
   - GUI clears existing project state (representations, preview, UI state)
   - Loads all TBC representations for preview
   - Restores DAG structure with node positions
   - Updates UI to reflect project state

8. **New Project**: User creates new project while existing project is loaded
   - GUI closes any open DAG editor window first
   - GUI clears existing project state via `clear_project()`
   - Creates fresh empty project
   - Updates window titles

**Quick Load** (backward compatibility): File → Open TBC (Quick) creates a temporary project with single source for rapid inspection without saving.

---

## 19. Incremental Implementation Order

1. Main window skeleton
2. SourceSet + Source models
3. Sources manager UI
4. Active source selection
5. Read-only DAG viewer
6. Field preview widget
7. Scrubbing & navigation
8. Multi-source preview
9. Alignment visualization
10. Provenance tools

---

## 20. Key Design Rules

* Files are implementation details; **sources are first-class**
* Navigation operates on **logical time**, not files
* Rendering operates on **fields**, not frames
* GUI never owns core processing logic
* Projects are the unit of work; all processing happens within a project context
* Project files are portable and tool-agnostic (shared between GUI and CLI)

---
