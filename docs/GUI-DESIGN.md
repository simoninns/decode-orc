# orc-gui - Design and Implementation Status

## Implementation Status (December 2025)

### ✅ Completed Features

**Core GUI Framework**
- Qt6-based application with QMainWindow
- Command-line argument support for loading TBC sources at startup
- DAG editor menu disabled until TBC source is loaded
- Menu bar with File (Open TBC, Quit) and Tools (DAG Editor) menus
- Field/frame preview with mode-aware navigation (moves by 1 field in field view, 2 fields in frame view)

**Interactive DAG Editor**
- QGraphicsView-based DAG visualization
- Visual node representation with input/output connection points
- Drag-to-connect edge creation (drag from output → input)
- Node movement and repositioning
- START node positioned on left side (-450, 0) for natural left-to-right flow
- START node displays source number and source name when TBC is loaded
- Selectable nodes and edges (edges highlighted when selected)
- DAG editor only available after loading a TBC source

**Node Management**
- Add nodes via context menu or keyboard
- Delete nodes via context menu or Del/Backspace key
- Change node type (stage) via context menu
- Nodes deleted automatically remove connected edges

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

**Error Handling**
- Methods return bool with optional error strings
- QMessageBox warnings for failed operations
- Null pointer validation throughout
- Try-catch blocks for exception safety

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
- Multiple START nodes for different sources
- Source alignment visualization
- Field fingerprinting UI

**Advanced Parameters**
- Parameter groups and categories
- Conditional parameters (visibility depends on other values)
- Parameter validation with real-time feedback
- Parameter presets and templates

---

## Purpose

This document defines the GUI design for **ld-decode-orc**, implemented in **Qt6 / C++**. It consolidates all GUI-related design decisions into a single reference suitable for beginning implementation.

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

## 18. Incremental Implementation Order

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

## 19. Key Design Rules

* Files are implementation details; **sources are first-class**
* Navigation operates on **logical time**, not files
* Rendering operates on **fields**, not frames
* GUI never owns core processing logic

---
