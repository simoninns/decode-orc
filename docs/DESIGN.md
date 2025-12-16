# ld-decode-orc Design

DRAFT

## Status

**Design document – implementation-oriented**

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
- Raw TBC fields
- Dropout-corrected fields
- Stacked or filtered fields

Each representation includes:

- FieldID sequence
- Field descriptors (parity, PAL/NTSC)
- Sample access API
- Provenance information

All further analysis and processing references a specific representation.

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

## 8. CLI and GUI Model

### 8.1 CLI

The CLI:
- Loads a DAG definition
- Executes the DAG
- Inspects artifacts and observations
- Imports and exports decision artifacts

No processing logic exists in the CLI itself.

---

### 8.2 GUI

The GUI:
- Edits the DAG
- Visualizes video fields
- Displays observer outputs
- Allows manual decision editing
- Triggers partial re-execution

The GUI never mutates signal data directly.

---

## 9. Legacy Tool Mapping

| Legacy Tool | New Role |
|------------|----------|
| ld-process-vbi | VBI Observer |
| ld-process-vits | VITS Observer |
| ld-dropout-detect | Dropout Observer |
| ld-dropout-correct | Dropout Correction Stage |
| ld-process-efm | EFM Signal Transformer |
| ld-discmap | Disc Interpretation Stage |
| ld-disc-stacker | Stacking Stage |
| ld-export-metadata | Export Stage |

---

## 10. Implementation Roadmap

### Phase 1 – Foundations
- FieldID model
- Video field representation abstraction
- Artifact identity and provenance
- Minimal DAG executor

### Phase 2 – Observers
- Observer framework
- Dropout observer
- VBI observer
- VITS observer
- Hint ingestion from TBC metadata

### Phase 3 – Decisions and Correction
- Decision artifact schema
- Manual dropout editing
- Dropout correction stage
- Partial DAG re-execution

### Phase 4 – Auxiliary Domains
- PCM alignment
- EFM processing
- Cross-domain inspection

### Phase 5 – Aggregation and Interpretation
- Stacking stage
- Disc mapping
- Aggregated VBI/VITS refinement

### Phase 6 – Tooling
- Complete CLI
- GUI prototype
- Visualization tooling
- Migration utilities

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

