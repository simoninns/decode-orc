# decode-orc - Design

DRAFT

## Status

**Design document – implementation-oriented**

**Phase 1, 2, and 3 implementation complete** (December 2025)

This document defines the architecture for a next-generation ld-decode tool chain.  
It is intended to guide implementation and future contribution, not to describe UI or UX in detail.

---

## 1. Motivation

The existing ld-decode tool chain is built as a Unix-style pipeline where each tool:

- Consumes a full TBC video file
- Produces a modified metadata database
- Optionally emits a new TBC file

While functional, this model has limitations:

- Poor support for branching, inspection, and partial reprocessing
- Metadata tightly coupled to execution order
- Manual intervention (e.g. dropout marking) is difficult to integrate cleanly
- No unified model for CLI and GUI use

This design replaces the pipeline model with a **single, explicit DAG-based architecture** that cleanly separates signal processing, analysis, and user decisions.

---

## 2. Core Architectural Principles

### 2.1 Single DAG per Processing Run

Each run is described by a single Directed Acyclic Graph (DAG):

- Nodes represent **signal-transforming stages**
- Edges represent **artifact dependencies**
- The DAG is static and declarative
- Execution order is derived from dependencies

There are no per-field DAGs and no dynamic pipeline mutation (i.e. the structure of the processing pipeline (DAG) is fixed before execution and does not change while the pipeline is running)

---

### 2.2 FieldID as the Fundamental Coordinate System

The authoritative coordinate system is a **monotonic sequence of FieldIDs** derived from the input TBC.

Properties:

- Unique and strictly ordered
- Not timestamps
- Not assumed to be uniformly spaced
- Fields may be missing, duplicated, or discontinuous
- Represents capture order, not playback time

All time-varying data (video, PCM, EFM, metadata) is associated with one or more FieldIDs.

**Single Field Execution Model:**

For GUI preview and inspection, the system operates on individual fields:
- GUI requests a specific FieldID at a specific DAG node
- orc-core executes the DAG chain for ONLY that single field
- No iteration through the entire source
- This keeps the structure simple and responsive

For final output (batch processing to sink), the system iterates through all available fields as a separate operation.

**See**: `DATA MODEL.md` Section 2.1 for complete API specification and implementation details.

---

### 2.3 Three-Class Processing Model

All functionality falls into exactly one of the following categories:

| Category | Description | Appears in DAG |
|--------|-------------|---------------|
| Signal Transformers | Produce new signal representations | Yes |
| Observers | Derive metadata from signals | No |
| Decisions | Represent user or policy intent | No |

This separation is strict and intentional.

---

## 3. Signal Representations

### 3.1 Video Field Representations

A **Video Field Representation** is any immutable artifact that provides access to video field samples.

Examples:
- Raw TBC fields (implemented as `TBCVideoFieldRepresentation` in Phase 1)
- Dropout-corrected fields (planned)
- Stacked or filtered fields (planned)

Each representation includes:

- FieldID sequence
- Field descriptors (parity, PAL/NTSC)
- Sample access API (line-level and field-level)
- Provenance information

All further analysis and processing references a specific representation.

**See**: `DATA MODEL.md` Section 4 for complete API specification and implementation details.

---

### 3.2 Auxiliary Signal Domains

#### Analogue PCM
- Stored as chunks aligned to FieldIDs
- Chunk sizes may vary per field
- Drift and jitter are preserved as data

#### EFM
- Stored as symbol or bitstream chunks aligned to FieldIDs
- Decoder state may span multiple fields
- Interpretation (audio vs data) is metadata

---

## 4. Observers

### 4.1 Definition

Observers are deterministic analyses attached to a video field representation.

Observers:
- Do not modify signal data
- Do not appear as DAG nodes
- Are derived automatically
- Are invalidated when their inputs change

Examples:
- Dropout detection
- VBI extraction
- VITS extraction
- Quality metrics

---

### 4.2 Observer Inputs

Observers consume:

1. A video field representation (mandatory)
2. Optional **hint metadata**, such as:
   - TBC-provided dropout indicators
   - RF-level confidence flags
   - Capture-side error markers

Hints are advisory and never authoritative.

---

### 4.3 Observer Outputs

Observers produce **observations** keyed by FieldID.

Each observation includes:
- FieldID
- Spatial or logical extent
- Observation type
- Confidence
- Detection basis:
  - sample-derived
  - hint-derived
  - corroborated
- Observer version and parameters

Observations are read-only, deterministic, and cacheable.

---

### 4.4 Hint Handling Rules

Observers must obey the following rules:

1. Hints may bias detection thresholds
2. Hints may seed candidate regions
3. Hints must not override contradictory sample evidence (i.e. hints do not determine alignment directly; they restrict and prioritize where expensive or uncertain alignment work is attempted)
4. Sample-derived evidence has priority
5. Provenance must record hint usage (The system must explicitly record whether, and how, hints influenced an alignment result)

---

## 5. Decisions (Manual or Policy Input)

### 5.1 Definition

Decisions represent user or policy intent and are explicit, versioned artifacts.

Examples:
- Manual dropout additions
- Removal of false positives
- Policy-based correction rules

Decisions never modify signal data directly.

---

### 5.2 Dropout Decisions

Dropout decisions are expressed as deltas against observer output.

Each decision includes:
- FieldID
- Line and sample range
- Action:
  - add
  - remove
  - modify
- Optional notes

---

## 6. Signal-Transforming Stages (DAG Nodes)

Only stages that produce new signal representations appear in the DAG.

### 6.1 Dropout Correction

**Inputs**
- Video field representation
- Dropout decisions
- Dropout observations (implicit)

**Outputs**
- New video field representation with corrected samples

---

### 6.2 Stacking

**Inputs**
- One or more video field representations
- Alignment metadata

**Outputs**
- New stacked video field representation

---

### 6.3 Export

**Inputs**
- Selected video field representation
- Associated observations and metadata

**Outputs**
- Files (video, metadata, audio)

---

## 7. Metadata and Storage

### 7.1 Metadata Store

Metadata is stored in a structured, queryable store (e.g. SQLite).

Requirements:
- Indexed by FieldID
- Scoped to artifact identity
- Versioned
- Provenance-aware

Existing ld-decode metadata schemas may be reused or extended as required, but are not necessarily the inter-tool communication mechanism (unlike the current ld-decode-tools).

---

### 7.2 Caching

- Signal-transforming stages are cached by input artifact IDs and parameters
- Observers are cached per video field representation
- Decision artifacts are never recomputed

---

## 8. Project Files

### 8.1 Purpose

A **project** encapsulates all information needed to process one or more TBC sources:

- Source TBC file paths with unique source IDs
- DAG structure (nodes, edges, parameters)
- Each Source node in the DAG references a specific source by source_id

Projects are the unit of work for both CLI and GUI tools.

---

### 8.2 File Format

Projects are stored as `.orc-project` files in YAML format.

**Structure**:

```yaml
# ORC Project File
# Version: 1.0

project:
  name: my_capture
  version: "1.0"

sources:
  - id: 0
    path: /path/to/capture.tbc
    name: capture
  - id: 1
    path: /path/to/capture2.tbc
    name: capture2

dag:
  nodes:
    - id: start_0
      stage: Source
      display_name: "Source: capture"
      x: -450.0
      y: 0.0
      source_id: 0
    - id: start_1
      stage: Source
      display_name: "Source: capture2"
      x: -450.0
      y: 200.0
      source_id: 1
    - id: dropout_correct
      stage: DropoutCorrect
      display_name: "Dropout Correction"
      x: -200.0
      y: 0.0
      parameters:
        max_dropout_length:
          type: uint32
          value: 10
  edges:
    - from: start_0
      to: dropout_correct
```

**Key properties**:

- `sources`: Array of source definitions
  - `id`: Unique integer identifier within project
  - `path`: Path to TBC file (may be relative to project file)
  - `name`: Display name for source
- `dag.nodes`: Array of DAG nodes
  - Source nodes include `source_id` to reference a source
  - Source nodes include `display_name` (e.g., "Source: capture")
  - Non-Source nodes have `source_id: -1` (or omitted)
  - All nodes have `stage` field identifying node type (Source, DropoutCorrect, etc.)
  - `parameters`: Stage-specific parameters with type information
- `dag.edges`: Array of edges
  - `from`: Source node ID
  - `to`: Target node ID

---

### 8.3 Implementation

Project file I/O is handled by `orc-core` in the `orc::project_io` namespace:

```cpp
namespace orc::project_io {
    // Project file I/O
    Project load_project(const std::string& filename);
    void save_project(const Project& project, const std::string& filename);
    Project create_single_source_project(const std::string& tbc_path, 
                                          const std::string& project_name = "");
    
    // Project state management
    void clear_project(Project& project);  // Reset to empty state
    
    // Source management
    int32_t add_source_to_project(Project& project, const std::string& path, 
                                   const std::string& display_name);
    bool remove_source_from_project(Project& project, int32_t source_id);
    
    // DAG node operations
    std::string add_node(Project& project, const std::string& stage_name,
                         const std::string& display_name, double x, double y,
                         int32_t source_id = -1);
    bool remove_node(Project& project, const std::string& node_id);
    bool change_node_type(Project& project, const std::string& node_id,
                          const std::string& new_stage_name);
    bool can_change_node_type(const Project& project, const std::string& node_id,
                              const std::string& new_stage_name);
    bool set_node_parameters(Project& project, const std::string& node_id,
                             const std::map<std::string, ParameterValue>& parameters);
    bool set_node_position(Project& project, const std::string& node_id,
                           double x, double y);
    
    // DAG edge operations
    bool add_edge(Project& project, const std::string& from_node_id,
                  const std::string& to_node_id);
    bool remove_edge(Project& project, const std::string& from_node_id,
                     const std::string& to_node_id);
}
```

**Modification Tracking**:

The `Project` struct includes a `is_modified` flag (mutable, not persisted) to track unsaved changes:

- Automatically set by all CRUD operations (add/remove nodes/edges, parameter/position changes)
- Cleared by `save_project()` and `load_project()`
- Queried via `has_unsaved_changes()`
- Cleared explicitly via `clear_modified_flag()`

**Design constraints**:

- Format is tool-agnostic (shared between `orc-gui` and `orc-process`)
- Self-contained and portable
- Human-readable for version control
- Extensible for future metadata
- All validation in core (no GUI business logic)

---

### 8.4 Multi-Source Support

While the initial implementation supports a single source, the format is designed for multiple sources:

- Each source has a unique `source_id`
- Each Source node references exactly one source
- Multiple Source nodes enable multi-source processing (e.g., stacking)
- Source alignment and fingerprinting metadata will be added in future versions

### 8.5 Node Type System

Nodes in the DAG are classified by connectivity pattern:

**Node Types** (defined in `orc-core`):
- **SOURCE**: No inputs, produces outputs (e.g., Source nodes for TBC input)
- **SINK**: Consumes inputs, no outputs (e.g., export to file)
- **TRANSFORM**: One input, one output (e.g., DropoutCorrect, Passthrough)
- **SPLITTER**: One input, multiple outputs (e.g., fanout for parallel processing)
- **MERGER**: Multiple inputs, one output (e.g., stacking, blending)
- **COMPLEX**: Multiple inputs, multiple outputs (e.g., advanced processing)

**Node Type Metadata** (`orc::NodeTypeInfo`):
- `stage_name`: Type identifier ("Source", "DropoutCorrect", etc.)
- `display_name`: Human-readable name ("Source", "Dropout Correction", etc.)
- `min_inputs`, `max_inputs`: Input port constraints
- `min_outputs`, `max_outputs`: Output port constraints
- `user_creatable`: Whether users can add this node type via GUI

**Current Implemented Stages**:
- `Source` (SOURCE): TBC input - auto-created by core, not user-creatable
- `Passthrough` (TRANSFORM): No-op for testing - 1 input, 1 output
- `DropoutCorrect` (TRANSFORM): Dropout correction - 1 input, 1 output
- `PassthroughSplitter` (SPLITTER): Test fanout - 1 input, 3 outputs
- `PassthroughMerger` (MERGER): Test merge - 2-8 inputs, 1 output
- `PassthroughComplex` (COMPLEX): Test multi-path - 2-4 inputs, 2-4 outputs

**GUI Integration**:
- GUI queries `orc::get_node_type_info()` to determine rendering
- Connection validation via `orc::is_connection_valid()`
- "Add Node" menu filters by `user_creatable` flag

---

## 9. CLI and GUI Model

### 9.1 CLI

The CLI:
- Loads a project file
- Loads a DAG definition (standalone or from project)
- Executes the DAG
- Inspects artifacts and observations
- Imports and exports decision artifacts

No processing logic exists in the CLI itself.

---

### 9.2 GUI

The GUI:
- Creates and manages projects
- Adds/removes sources to/from projects
- Edits the DAG visually (add/remove/connect nodes, change parameters, set positions)
- Visualizes video fields
- Displays observer outputs
- Allows manual decision editing
- Triggers partial re-execution
- Saves project state (sources + DAG)

The GUI never mutates signal data directly.

**Architecture**: Complete separation of concerns
- **Core layer** (`orc-core`): All business logic, validation, and data structures
- **GUI layer** (`orc-gui`): Pure presentation, Qt widgets, signals/slots
- All DAG operations go through core CRUD API:
  - `add_node()`, `remove_node()`, `change_node_type()`
  - `set_node_parameters()`, `set_node_position()`
  - `add_edge()`, `remove_edge()`
  - `can_change_node_type()` for validation
- Automatic modification tracking in core (is_modified flag)
- Signal-based UI updates (dagModified → projectModified → updateUIState)

**See**: `docs/GUI-DESIGN.md` for complete GUI architecture documentation.

---

## 10. Legacy Tool Mapping

| Legacy Tool | New Role | Status |
|------------|----------|--------|
| ld-process-vbi | 6 VBI Observers (Biphase, VITC, ClosedCaption, VideoId, FmCode, WhiteFlag) | ✅ Implemented |
| ld-process-vits | VITS Quality Observer | ✅ Implemented |
| ld-dropout-detect | Dropout Observer | Planned |
| ld-dropout-correct | Dropout Correction Stage | Planned |
| ld-process-efm | EFM Signal Transformer | Planned |
| ld-discmap | Disc Interpretation Stage | Planned |
| ld-disc-stacker | Stacking Stage | Planned |
| ld-export-metadata | Export Stage | Planned |

---

## 10. Implementation Roadmap

### Phase 1 – Foundations
**Goal**: Establish core abstractions and infrastructure

**Components**:
- FieldID coordinate system
- Video field representation abstraction
- Artifact identity and provenance
- Minimal DAG executor
- TBC file I/O infrastructure
- Metadata reading (SQLite)

### Phase 2 – Observers
**Goal**: Implement observation framework and VBI/VITS analysis

**Components**:
- Observer framework (base classes and confidence system)
- VITS Quality Observer (white SNR, black PSNR)
- 6 VBI Observers:
  - BiphaseObserver (Manchester-coded picture numbers)
  - VitcObserver (VITC timecode)
  - ClosedCaptionObserver (CEA-608 captions)
  - VideoIdObserver (NTSC IEC 61880)
  - FmCodeObserver (NTSC FM-coded data)
  - WhiteFlagObserver (NTSC white flag)
- orc-process CLI tool with YAML pipelines

### Phase 3 – Dropout Detection and Correction
**Goal**: Implement dropout detection, decision framework, and correction stages

**Components**:
- Dropout observer (amplitude detection + hint integration)
- Decision artifact schema
- Manual dropout editing (ADD/REMOVE/MODIFY)
- Dropout correction stage (intra/interfield)
- Stage execution in orc-process
- Partial DAG re-execution

### Phase 4 – Auxiliary Domains
**Goal**: Support audio and data domains

**Components**:
- PCM alignment
- EFM processing
- Cross-domain inspection

### Phase 5 – Aggregation and Interpretation
**Goal**: Multi-source processing and disc mapping

**Components**:
- Field fingerprinting and alignment
- Stacking stage
- Disc mapping
- Aggregated VBI/VITS refinement

### Phase 6 – Tooling
**Goal**: Complete user-facing tools

**Components**:
- Complete CLI functionality
- GUI prototype
- Visualization tooling
- Migration utilities from legacy tools

---

## 11. Implementation Progress

### Phase 1 – Foundations ✅ COMPLETED
**Status**: Fully implemented and tested (December 2025)

**Delivered Components**:

#### Core Abstractions
- **FieldID model** ([orc/core/include/field_id.h](orc/core/include/field_id.h))
  - FieldID class with monotonic uint64_t value
  - FieldIDRange for representing field sequences
  - Full comparison, arithmetic, and hash support

- **Video field representation** ([orc/core/include/video_field_representation.h](orc/core/include/video_field_representation.h))
  - Abstract VideoFieldRepresentation base class
  - FieldDescriptor with parity, line count, samples per line
  - VideoFormat enum (PAL, NTSC)
  - Zero-copy line access and full-field access

- **Artifact identity and provenance** ([orc/core/include/artifact.h](orc/core/include/artifact.h))
  - ArtifactID for content-addressed identification
  - Provenance struct tracking stage, parameters, inputs
  - Immutable shared_ptr usage pattern

- **DAG executor** ([orc/core/include/dag_executor.h](orc/core/include/dag_executor.h))
  - DAG class with node/edge management
  - Topological sort with cycle detection
  - Artifact caching and progress callbacks

#### TBC I/O Infrastructure
- **TBCReader** ([orc/core/include/tbc_reader.h](orc/core/include/tbc_reader.h))
  - Binary TBC file reader with LRU caching (100 fields)
  - Line-level and field-level access
  - 16-bit unsigned sample format

- **TBCMetadataReader** ([orc/core/include/tbc_metadata.h](orc/core/include/tbc_metadata.h))
  - SQLite metadata database reader
  - VideoParameters, FieldMetadata, Dropout records
  - VBI, VITC, PCM, VITS, Closed caption data
  - Pimpl pattern with sqlite3 C API

- **TBCVideoFieldRepresentation** ([orc/core/include/tbc_video_field_representation.h](orc/core/include/tbc_video_field_representation.h))
  - Concrete implementation backed by TBC files
  - Factory function for easy instantiation

#### Testing and Validation
- Comprehensive unit tests (FieldID, DAG, TBC I/O)
- Integration tests with real LaserDisc captures:
  - 17 test files (9 PAL, 8 NTSC)
  - ~3,256 fields processed
  - PAL CAV/CLV and NTSC CAV/CLV coverage
  - All metadata types validated

#### Build System
- CMake 3.20+ with C++17
- Separate targets: core library, CLI, tests
- SQLite3 integration
- SPDX GPL-3.0-or-later headers

### Phase 2 – Observers ✅ COMPLETED
**Status**: Fully implemented and tested (December 2025)

**Delivered Components**:

#### Observer Framework
- **Base classes** ([orc/core/include/observer.h](orc/core/include/observer.h))
  - Observer base with process_field() method
  - Observation base with field_id and confidence
  - DetectionBasis enum (SAMPLE_DERIVED, HINT_DERIVED, CORROBORATED)
  - ConfidenceLevel enum (NONE, LOW, MEDIUM, HIGH)

#### VITS Quality Observer
- **VITSQualityObserver** ([orc/core/include/vits_observer.h](orc/core/include/vits_observer.h))
  - White flag SNR calculation (dB)
  - Black level PSNR calculation (dB)
  - PAL: Lines 19 (white), 22 (black)
  - NTSC: Lines 20/13 (white), Line 1 (black)
  - Configurable via YAML

#### VBI Observers (6 total)
Each observer implements specific VBI standard with proper validation:

1. **BiphaseObserver** - IEC 60586/60587 Manchester encoding
   - Lines 16-18 (PAL/NTSC)
   - Picture numbers, CLV timecodes, chapter markers
   - **Validation**: 100% accuracy (2,854/2,854 fields)

2. **VitcObserver** - ITU-R BR.780-2, SMPTE ST 12-1
   - Lines 6-22 (PAL), 10-20 (NTSC)
   - Timecode with 8-bit CRC validation
   - **Status**: Implemented, awaiting test data

3. **ClosedCaptionObserver** - ANSI/CTA-608-E (CEA-608)
   - Line 21 (NTSC), Line 22 (PAL)
   - Odd parity validation
   - **Validation**: 84% accuracy (682/810 fields)

4. **VideoIdObserver** - IEC 61880:1998
   - Line 20 (NTSC field 1)
   - Aspect ratio, CGMS-A, APS with CRC-6
   - **Status**: Implemented, awaiting test data

5. **FmCodeObserver** - IEC 60587 §10.2
   - Line 10 (NTSC field 1)
   - 40-bit FM code with parity
   - **Status**: Implemented, awaiting test data

6. **WhiteFlagObserver** - IEC 60587 §10.2.4
   - Line 11 (NTSC field 1)
   - Binary threshold detection
   - **Validation**: Working correctly

**Common VBI utilities**: Zero-crossing detection, transition mapping, debounce filtering, parity checking

#### orc-process CLI Tool
- **Command-line interface** ([orc/cli/orc_process.cpp](orc/cli/orc_process.cpp))
  - YAML pipeline configuration
  - Observer execution with progress tracking
  - SQLite metadata regeneration
  - Compatible with ld-analyse
  - Database schema: capture, field_record, vbi, vitc, closed_caption, vits_metrics

**Testing**: All observers validated against legacy ld-process-vbi tool where test data available

### Phase 3 – Dropout Detection and Correction ✅ COMPLETED
**Status**: Fully implemented and tested (December 2025)

**Delivered Components**:

#### Decision Framework
- **DropoutDecision** ([orc/core/include/dropout_decision.h](orc/core/include/dropout_decision.h))
  - User decision actions: ADD, REMOVE, MODIFY
  - Per-field decision collection
  - Applied as deltas to TBC hints
  - Notes field for documentation
  - DropoutRegion struct with DetectionBasis

#### Correction Stage
- **DropoutCorrectStage** ([orc/core/include/dropout_correct_stage.h](orc/core/include/dropout_correct_stage.h))
  - Signal-transforming stage (appears in DAG)
  - Uses TBC metadata hints directly (no observer needed)
  - Intrafield correction (nearby lines, same field)
  - Interfield correction (same line, opposite field)
  - Quality-based replacement line selection using variance metric
  - Handles multiple dropouts per line correctly
  - Legacy-compatible options:
    * overcorrect_extension (0-24 samples)
    * intrafield_only (bool)
    * reverse_field_order (bool)
    * max_replacement_distance (1-10 lines)
    * match_chroma_phase (bool)

- **CorrectedVideoFieldRepresentation** - Wrapper with sparse storage for corrected lines
  - Stores only modified lines in map
  - Falls back to source representation for uncorrected lines
  - Accumulates multiple corrections per line

#### Pipeline Integration
- Stage execution in orc-process with corrected TBC generation
- TBC output regenerated from field representations (not copied)
- Metadata tracking: dropout table empty if corrected, preserved if not
- Line number conversion: database uses 1-indexed, internal uses 0-indexed
- Example pipelines:
  * [examples/dropout-correct.yaml](examples/dropout-correct.yaml) - Basic correction
  * [examples/dropout-correct-with-decisions.yaml](examples/dropout-correct-with-decisions.yaml) - Manual decisions
  * [examples/dropout-correct-overcorrect.yaml](examples/dropout-correct-overcorrect.yaml) - Aggressive mode
  * [examples/dropout-correct-intrafield.yaml](examples/dropout-correct-intrafield.yaml) - Conservative mode

**Testing**: Validated with Domesday PAL data (400 fields, 286 corrected, 550 dropouts). Corrections verified visually in ld-analyse.

**Key Fixes During Implementation**:
- Multiple dropouts per line: accumulate corrections instead of overwriting
- White level: use metadata value (54016 for PAL) not hardcoded 16384
- Line indexing: convert database 1-indexed to internal 0-indexed

**Next Steps**: 
- Complete stage execution in orc-process main loop
- Integration testing with real dropout data
- Multi-source correction testing

### Phase 4 – Auxiliary Domains (PLANNED)
**Status**: Not started

### Phase 5 – Aggregation and Interpretation (PLANNED)
**Status**: Not started

### Phase 6 – Tooling (IN PROGRESS)
**Status**: Partial implementation (December 2025)

**Delivered Components**:

#### GUI Foundation
- **MainWindow** ([orc/gui/mainwindow.cpp](orc/gui/mainwindow.cpp))
  - Project lifecycle: New, Open, Save, Save As
  - Modification tracking with window title indicator (asterisk)
  - Recent projects list
  - Source management via GUI dialog
  - Launches DAG Editor window
  - State clearing when switching/creating projects

- **DAGEditorWindow** ([orc/gui/dageditorwindow.cpp](orc/gui/dageditorwindow.cpp))
  - Visual DAG editing interface
  - Node creation via context menu (filtered by user_creatable flag)
  - Node deletion and type changing
  - Edge creation via drag-and-drop
  - Node positioning with mouse
  - Modification status in window title
  - Signal chain: dagModified → projectModified → main window update

- **DAGViewerWidget** ([orc/gui/dagviewerwidget.cpp](orc/gui/dagviewerwidget.cpp))
  - Custom QGraphicsView for DAG visualization
  - Node rendering with type-specific colors
  - Edge rendering with bezier curves
  - Interactive node dragging
  - Context menus for add/delete/change operations
  - Connection validation via core API

- **GUIProject** ([orc/gui/guiproject.cpp](orc/gui/guiproject.cpp))
  - Pure wrapper around `orc::Project`
  - No business logic (all delegated to core)
  - Signal emissions for Qt integration
  - CRUD operations call `orc::project_io` functions

**Architecture Principles**:
- Complete separation: GUI is purely presentational
- All validation in core layer
- Signal-based UI updates (no polling)
- Automatic modification tracking
- State management via core APIs

**Planned Components**:
- Field visualization widget
- Observer output display
- Manual decision editing UI
- Pipeline execution controls
- Migration utilities from legacy tools

---

## 11. Design Invariants

The following are non-negotiable:

- FieldID is the primary coordinate system
- Observers do not appear in the DAG
- Signal data is immutable
- Manual edits are explicit artifacts
- Hints bias analysis but do not override evidence
- Reproducibility is mandatory

---

## 12. Deferred Topics

The following are intentionally deferred:

- Exact on-disk chunk formats
- Observer parallelism granularity
- Long-term cache eviction strategy
- GUI rendering implementation

These do not block initial implementation.

---

## 13. Alignment Mapping and Field Fingerprinting

### Problem Statement

Certain processing stages (notably *disc stacking*) operate on multiple captures of the *same mastered content* taken from different physical LaserDiscs. These captures may differ due to:

* Player skips or jumps
* Repeated fields or dropped fields
* Capture start/stop offsets
* Disc damage or playback instability

As a result, simple positional alignment (e.g. `FieldID N` from source A corresponds to `FieldID N` from source B) is invalid. A robust alignment mechanism is required to map each captured field onto a canonical content timeline before stacking or comparison can occur.

### MasterFieldID (Canonical Timeline)

Introduce a second coordinate system in addition to per-representation `FieldID`:

* **FieldID**: Monotonic index local to a specific capture/representation (capture order).
* **MasterFieldID**: Monotonic index representing the authored/mastered content timeline.

All multi-source operations (stacking, comparison, voting) operate in **MasterFieldID space**.

### Alignment Mapping Artifact

The `ld-discmap` stage SHALL produce an immutable **AlignmentMap** artifact for each input representation.

For a given representation `R`:

```
AlignmentMap[R][FieldID] = {
  master_id: MasterFieldID | null,
  phase: alignment offset / parity info,
  confidence: 0.0 – 1.0,
  flags: [ duplicate | gap | jump | damaged | uncertain ],
  provenance: method used (VBI, fingerprint, manual)
}
```

The inverse mapping is also derivable:

```
InverseMap[R][MasterFieldID] = list of { FieldID, confidence }
```

Explicit gaps, duplicates, and discontinuities are therefore represented directly rather than inferred.

### Field Fingerprinting (Observers)

Alignment is derived using **field-level fingerprints**, computed by deterministic Observer modules. These Observers do not modify signal data.

Typical fingerprints include:

* **Video fingerprint**: compact, noise-tolerant hash of luma/chroma content
* **VBI fingerprint**: decoded VBI data when present (strong anchors)
* **Auxiliary markers**: VITS, test signals, or other known patterns
* **Optional audio landmarks**: coarse alignment cues when applicable

Fingerprints are stored as Observations keyed by `FieldID`.

### Alignment Process (Conceptual)

1. Extract fingerprints for all fields in each representation.
2. Identify high-confidence anchors between captures and the master timeline.
3. Perform monotonic sequence alignment between anchors, allowing:

   * Insertions (repeated fields)
   * Deletions (skipped fields)
   * Piecewise alignment across jumps or discontinuities
4. Emit an AlignmentMap with per-field confidence and segmentation.

Manual alignment decisions may be introduced as explicit **Alignment Decisions** to override or reinforce automatic results.

### Impact on Stacking

The stacking stage consumes:

* Multiple field representations
* Their corresponding AlignmentMaps

For each `MasterFieldID`, candidate fields from all sources are gathered, filtered by confidence and flags, and combined according to stacking policy. The stacked output is indexed primarily by `MasterFieldID`, yielding a timeline-stable representation suitable for further processing.

This mechanism enables robust multi-disc enhancement despite skips, jumps, and capture inconsistencies.

