# DATA_MODEL.md

DRAFT

## ld-decode Next-Generation Tool Chain

## Status

**Normative – implementation reference**

This document defines the canonical data model used by the ld-decode next-generation tool chain.  
All core components MUST conform to this model.

---

## 1. Scope

This document defines:

- The fundamental coordinate system (`FieldID`)
- Signal representations (video, PCM, EFM)
- Derived observations (dropout, VBI, VITS)
- Decision artifacts (manual intervention)
- Artifact identity and provenance
- Metadata persistence requirements

This document does **not** define:
- Processing algorithms
- UI representations
- Storage optimizations

---

## 2. Fundamental Coordinate System

### 2.1 FieldID

`FieldID` is the fundamental coordinate for all time-varying data.

**Definition**
- A strictly ordered, monotonic identifier
- Represents capture order
- Does not imply time, duration, or continuity

**Properties**
- Unique within a capture
- Non-negative integer
- Order is authoritative
- Gaps and repetition are permitted and meaningful

**Non-properties**
- Not a timestamp
- Not required to be contiguous
- Not convertible to time without interpretation

All video fields, PCM samples, EFM symbols, observations, and decisions MUST reference one or more `FieldID`s.

---

## 3. Artifact Identity and Provenance

### 3.1 Artifact Identity

Every persistent entity is an **Artifact**.

Each artifact has:
- `ArtifactID` (unique, stable)
- `ArtifactType`
- Creation parameters
- Input artifact references
- Tool / stage version

Artifacts are immutable.

---

### 3.2 Provenance

Provenance records:
- Input artifacts
- Parameters
- Tool versions
- Hint sources (if applicable)

Two artifacts with identical provenance MUST be bit-identical.

---

## 4. Video Signal Model

### 4.1 Video Field Representation

A **Video Field Representation** is an artifact that provides access to video field samples.

**Required properties**
- Ordered list of `FieldID`s
- Field descriptors
- Sample access for each field
- Provenance

Video field representations are immutable.

---

### 4.2 Field Descriptor

Each field has a descriptor:

- `FieldID`
- Parity (even / odd)
- Nominal standard (PAL / NTSC)
- Capture flags:
  - missing
  - repeated
  - damaged
  - synthetic (optional)

Descriptors MUST exist even if sample data is missing.

---

### 4.3 Field Samples

Field samples:
- Represent raw or processed analogue video signal
- Are not RGB or YUV frames
- Preserve sample-level timing irregularities

Sample layout is implementation-defined but must be deterministic.

---

## 5. Auxiliary Signal Domains

### 5.1 Analogue PCM Audio

PCM audio is represented as **Field-aligned chunks**.

Each chunk includes:
- One or more `FieldID`s
- Sample range
- Sample rate
- Offset relative to field start (if applicable)

Chunk sizes MAY vary per field.

Drift and jitter MUST be preserved.

---

### 5.2 EFM Data

EFM is represented as **Field-aligned symbol or bitstream chunks**.

Each chunk includes:
- One or more `FieldID`s
- Symbol or bit range
- Decoder state (if applicable)
- Error indicators

Interpretation (audio vs data) is metadata, not signal.

---

## 6. Observations (Derived Metadata)

### 6.1 Definition

Observations are deterministic, derived analyses attached to a video field representation.

Observations:
- Are read-only
- Are cacheable
- Are invalidated when inputs change
- Do not appear as DAG nodes

---

### 6.2 Observation Common Fields

All observations include:
- `FieldID`
- Observation type
- Spatial or logical extent
- Confidence score
- Detection basis:
  - sample-derived
  - hint-derived
  - corroborated
- Observer version
- Observer parameters

---

### 6.3 Dropout Observations

Represents suspected signal loss or corruption.

Fields:
- `FieldID`
- Line number
- Sample range (start, end)
- Confidence
- Detection basis

Dropout observations are advisory only.

---

### 6.4 VBI Observations

Represents decoded VBI data.

Fields:
- `FieldID`
- Line identifier
- Raw symbol data
- Interpreted values
- Decode confidence

VBI observations may be regenerated whenever the signal changes.

---

### 6.5 VITS Observations

Represents VITS measurements.

Fields:
- `FieldID`
- Metric type
- Measured value(s)
- Measurement confidence

VITS observations are purely descriptive.

---

## 7. Observer Hints

### 7.1 Definition

Hints are auxiliary metadata provided to observers to improve detection accuracy.

Hints are:
- Advisory
- Optional
- Never authoritative

---

### 7.2 Hint Structure

Each hint includes:
- `FieldID` or range
- Scope:
  - whole field
  - line range
  - unknown
- Hint type
- Confidence or severity (optional)
- Source identifier

---

### 7.3 Hint Rules

- Hints may bias detection thresholds
- Hints may seed candidate regions
- Hints MUST NOT override contradictory sample evidence
- Hint usage MUST be recorded in observation provenance

---

## 8. Decision Artifacts

### 8.1 Definition

Decisions represent user or policy intent.

Decisions:
- Are explicit artifacts
- Are immutable and versioned
- Influence signal-transforming stages

---

### 8.2 Dropout Decisions

Dropout decisions modify the interpretation of dropout observations.

Fields:
- `FieldID`
- Line number
- Sample range
- Action:
  - add
  - remove
  - modify
- Optional notes

Decisions apply only to correction stages.

---

## 9. Signal-Transforming Stages and Outputs

Only signal-transforming stages produce new video field representations.

These outputs:
- Reference input artifacts
- Reference decisions
- Trigger observer invalidation

Examples:
- Dropout-corrected fields
- Stacked fields

---

## 10. Metadata Persistence

### 10.1 Storage Requirements

Metadata storage MUST:
- Preserve FieldID indexing
- Preserve artifact identity
- Preserve provenance
- Support efficient querying

SQLite is the reference implementation.

---

### 10.2 Compatibility

Existing ld-decode metadata schemas may be:
- Reused directly
- Extended with artifact scoping
- Mapped into the new model

Backward compatibility is desirable but not required.

---

## 11. Invariants (Non-Negotiable)

- FieldID is the primary coordinate
- Signal artifacts are immutable
- Observations do not make decisions
- Decisions are explicit artifacts
- Observers are deterministic
- Provenance is complete

---

## 12. Open Items (Deferred)

- Exact sample storage format
- Chunk size optimization
- Long-term cache eviction
- Observer parallelism details

These do not block initial implementation.

---

