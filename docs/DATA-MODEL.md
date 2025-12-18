# decode-orc - Data Model

DRAFT

## Status

**Normative – implementation reference**

**Phase 1 data structures implemented and tested** (December 2025)

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

**Implementation**: `orc/core/include/field_id.h`

**C++ Definition**
```cpp
class FieldID {
    uint64_t value;
public:
    constexpr explicit FieldID(uint64_t v = 0);
    constexpr uint64_t get_value() const;
    
    // Comparison operators
    constexpr bool operator==(const FieldID& other) const;
    constexpr bool operator!=(const FieldID& other) const;
    constexpr bool operator<(const FieldID& other) const;
    constexpr bool operator<=(const FieldID& other) const;
    constexpr bool operator>(const FieldID& other) const;
    constexpr bool operator>=(const FieldID& other) const;
    
    // Arithmetic operations
    FieldID operator+(uint64_t offset) const;
    FieldID operator-(uint64_t offset) const;
    uint64_t operator-(const FieldID& other) const;
    FieldID& operator++();
    FieldID operator++(int);
    
    std::string to_string() const;
};

struct FieldIDRange {
    FieldID start;
    FieldID end;  // exclusive
    
    bool contains(FieldID id) const;
    uint64_t count() const;
};

// Hash support for containers
namespace std {
    template<> struct hash<orc::FieldID>;
}
```

**Properties**
- A strictly ordered, monotonic identifier
- Represents capture order
- Does not imply time, duration, or continuity
- Unique within a capture
- Non-negative integer (0 to 2^64-1)
- Order is authoritative
- Gaps and repetition are permitted and meaningful
- `constexpr` for compile-time operations where applicable
- Hashable for use in `std::unordered_map` and `std::unordered_set`

**Non-properties**
- Not a timestamp
- Not required to be contiguous
- Not convertible to time without interpretation

All video fields, PCM samples, EFM symbols, observations, and decisions MUST reference one or more `FieldID`s.

**Validation**: Tested with comprehensive unit tests in `orc/tests/test_field_id.cpp`

---

## 3. Artifact Identity and Provenance

### 3.1 Artifact Identity

Every persistent entity is an **Artifact**.

**Implementation**: `orc/core/include/artifact.h`

**C++ Definition**
```cpp
using ArtifactID = std::string;

struct Provenance {
    std::string stage_name;
    std::string stage_version;
    std::map<std::string, std::string> parameters;
    std::vector<ArtifactID> input_ids;
    std::chrono::system_clock::time_point created_at;
};

class Artifact {
public:
    virtual ~Artifact() = default;
    virtual std::string type_name() const = 0;
    
    ArtifactID artifact_id;
    Provenance provenance;
};
```

Each artifact has:
- `ArtifactID` (unique, stable string identifier)
- `ArtifactType` (returned by virtual `type_name()` method)
- Creation parameters (in `Provenance::parameters`)
- Input artifact references (in `Provenance::input_ids`)
- Tool / stage version (in `Provenance::stage_name` and `stage_version`)

Artifacts are immutable and passed via `std::shared_ptr<Artifact>`.

**Validation**: Tested in DAG executor tests (`orc/tests/test_dag_executor.cpp`)

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

**Implementation**: `orc/core/include/video_field_representation.h`

**C++ Definition**
```cpp
enum class VideoFormat {
    PAL,
    NTSC
};

struct FieldDescriptor {
    size_t line_count;
    size_t samples_per_line;
    bool is_first_field;  // true = first field, false = second field
};

class VideoFieldRepresentation : public Artifact {
public:
    virtual ~VideoFieldRepresentation() = default;
    
    // Field access
    virtual std::optional<FieldDescriptor> get_descriptor(FieldID field_id) const = 0;
    virtual const uint16_t* get_line(FieldID field_id, size_t line_num) const = 0;
    virtual std::vector<uint16_t> get_field(FieldID field_id) const = 0;
    
    // Metadata
    virtual FieldIDRange get_field_range() const = 0;
    virtual VideoFormat get_format() const = 0;
    
    std::string type_name() const override { return "VideoFieldRepresentation"; }
};
```

**Required properties**
- Ordered list of `FieldID`s (via `get_field_range()`)
- Field descriptors (via `get_descriptor()`)
- Sample access for each field:
  - `get_line()`: Returns const pointer for zero-copy access (returns nullptr for missing lines)
  - `get_field()`: Returns complete field as vector copy
- Provenance (inherited from `Artifact`)
- Video format (PAL or NTSC via `get_format()`)

Video field representations are immutable.

**Sample Format**
- 16-bit unsigned integers (`uint16_t`)
- Represents analogue video signal levels
- Not RGB or YUV frames
- Preserves sample-level timing irregularities

**Concrete Implementation**: `TBCVideoFieldRepresentation` in `orc/core/include/tbc_video_field_representation.h`

**Validation**: Tested with real PAL and NTSC TBC files in `orc/tests/test_real_tbc_data.cpp`

---

### 4.2 Field Descriptor

Each field has a descriptor:

**Implementation**: `FieldDescriptor` struct in `orc/core/include/video_field_representation.h`

**C++ Definition**
```cpp
struct FieldDescriptor {
    size_t line_count;         // Number of lines in this field
    size_t samples_per_line;   // Number of samples per line
    bool is_first_field;       // true = first field, false = second field
};
```

**Fields**:
- `line_count`: Number of lines in the field (typically 313 for PAL, 263 for NTSC)
- `samples_per_line`: Number of samples per line (varies by capture hardware)
- `is_first_field`: Field parity/order indicator (true for first field, false for second field)

**Extended Metadata** (via TBC metadata database):
Additional per-field metadata is available through `TBCMetadataReader`:
- `FieldID` mapping
- Capture flags (via confidence field)
- Pad field indicator
- Missing/repeated/damaged status (to be implemented in future phases)
- Synthetic field indicator (to be implemented in future phases)

Descriptors MUST exist even if sample data is missing.

**Implementation Note**: The current Phase 1 implementation provides basic parity through `is_first_field`. Future phases will add comprehensive capture flags as needed.

---

### 4.3 Field Samples

Field samples:
- Represent raw or processed analogue video signal
- Are not RGB or YUV frames
- Preserve sample-level timing irregularities

**Sample Format**:
- Type: `uint16_t` (16-bit unsigned integer)
- Range: 0-65535
- Stored sequentially: line-by-line, sample-by-sample
- Total samples per field: `line_count × samples_per_line`

**Access Patterns**:
1. **Line-level access** (zero-copy):
   ```cpp
   const uint16_t* line = representation->get_line(field_id, line_num);
   // Returns pointer to samples_per_line values, or nullptr if unavailable
   ```

2. **Field-level access** (copy):
   ```cpp
   std::vector<uint16_t> field = representation->get_field(field_id);
   // Returns complete field data as vector
   ```

**Storage** (current implementation):
- TBC files: Binary sequential storage, 16-bit little-endian per sample
- Caching: LRU cache in `TBCReader` (max 100 fields)
- Line-aligned access for efficient partial reads

Sample layout is implementation-defined but must be deterministic.

---

### 4.4 TBC Metadata (Phase 1 Implementation)

**Implementation**: `orc/core/include/tbc_metadata.h`

TBC files are accompanied by SQLite metadata databases containing comprehensive capture information.

**TBCMetadataReader C++ API**:
```cpp
class TBCMetadataReader {
public:
    explicit TBCMetadataReader(const std::string& db_path);
    ~TBCMetadataReader();
    
    std::optional<VideoParameters> read_video_parameters() const;
    std::optional<FieldMetadata> read_field_metadata(FieldID field_id) const;
    std::vector<DropoutRecord> read_dropouts(FieldID field_id) const;
    std::optional<VBIData> read_vbi(FieldID field_id) const;
    std::optional<VITCData> read_vitc(FieldID field_id) const;
    std::optional<PCMAudioParameters> read_pcm_audio_parameters() const;
    std::optional<VITSMetrics> read_vits_metrics(FieldID field_id) const;
    std::optional<ClosedCaptionData> read_closed_captions(FieldID field_id) const;
};
```

**Video Parameters** (from `capture` table):
```cpp
struct VideoParameters {
    bool is_pal;                    // PAL (true) or NTSC (false)
    size_t number_of_sequences;     // Number of capture sequences
    size_t fields_per_sequence;     // Fields per sequence
    size_t number_of_fields;        // Total field count
    size_t sample_rate;             // Samples per second (Hz)
    bool is_color;                  // Color (true) or monochrome (false)
    int lines_per_frame;            // Total lines (both fields)
    double aspect_ratio;            // Display aspect ratio
};
```

**Field Metadata** (from `field_record` table):
```cpp
struct FieldMetadata {
    bool is_first_field;            // Field parity
    int confidence;                 // Capture confidence (0-100)
    bool is_pad;                    // Pad field indicator
};
```

**Dropout Records** (from `drop_outs` table):
```cpp
struct DropoutRecord {
    size_t field_line;              // Line number within field
    size_t start_sample;            // Starting sample (startx)
    size_t end_sample;              // Ending sample (endx)
};
```

**VBI Data** (from `vbi` table):
```cpp
struct VBIData {
    std::array<uint32_t, 3> vbi_line_16;    // VBI data line 16
    std::array<uint32_t, 3> vbi_line_17;    // VBI data line 17  
    std::array<uint32_t, 3> vbi_line_18;    // VBI data line 18
    int picture_number;                      // Picture number (-1 if unavailable)
    int clv_hour;                            // CLV timecode hours (-1 if unavailable)
    int clv_minute;                          // CLV timecode minutes
    int clv_second;                          // CLV timecode seconds
    int clv_picture_number;                  // CLV picture number
};
```

**VITC Data** (from `vitc` table):
```cpp
struct VITCData {
    int frame_hour;                 // VITC hours
    int frame_minute;               // VITC minutes
    int frame_second;               // VITC seconds
    int frame_number;               // VITC frame number
};
```

**PCM Audio Parameters** (from `pcm_audio_parameters` table):
```cpp
struct PCMAudioParameters {
    size_t sample_rate;             // Audio sample rate (Hz)
    bool is_stereo;                 // Stereo (true) or mono (false)
    int bits_per_sample;            // Bit depth
};
```

**VITS Metrics** (from `vits_metrics` table):
```cpp
struct VITSMetrics {
    double white_flag_snr;          // White flag SNR (dB)
    double black_level;             // Black level
    int white_level;                // White level
};
```

**Closed Caption Data** (from `closed_captions` table):
```cpp
struct ClosedCaptionData {
    int data1;                      // First CC byte
    int data2;                      // Second CC byte
};
```

**Database Schema**: SQLite3 with tables `capture`, `field_record`, `drop_outs`, `vbi`, `vitc`, `pcm_audio_parameters`, `vits_metrics`, `closed_captions`

**Validation**: Tested with 6 real TBC files covering PAL/NTSC, CAV/CLV formats in `orc/tests/test_real_tbc_data.cpp`

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

**Status**: Observation framework design complete; implementation planned for Phase 2.

### 6.1 Definition

Observations are deterministic, derived analyses attached to a video field representation.

Observations:
- Are read-only
- Are cacheable
- Are invalidated when inputs change
- Do not appear as DAG nodes

**Phase 1 Note**: While the full Observer framework is not yet implemented, TBC metadata includes captured observations (dropouts, VBI, VITC, VITS) that will serve as hints for Phase 2 Observers. See Section 4.4 for available metadata.

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

**Phase 1 Implementation**: Dropout records from TBC metadata are available via `TBCMetadataReader::read_dropouts()`. See Section 4.4 for the `DropoutRecord` structure. These will be used as hints for the Phase 2 Dropout Observer.

---

### 6.4 VBI Observations

Represents decoded VBI data from Vertical Blanking Interval lines.

**Implementation Status**: Phase 2b complete with 6 VBI observers validated against legacy tool.

**Observer Types**:
1. **BiphaseObservation** (lines 16-18): Manchester-coded data (picture numbers, chapter markers, stop codes)
2. **VitcObservation** (lines 6-22 PAL, 10-20 NTSC): VITC timecode with CRC validation
3. **ClosedCaptionObservation** (line 21 NTSC, 22 PAL): CEA-608 closed captions with parity checking
4. **VideoIdObservation** (line 20 NTSC): IEC 61880 aspect ratio and copy protection with CRC-6
5. **FmCodeObservation** (line 10 NTSC): FM-coded metadata with sync validation
6. **WhiteFlagObservation** (line 11 NTSC): Binary flag for disc control

**Common Fields**:
- `FieldID` - Field identifier
- `confidence` - Decode confidence (HIGH/MEDIUM/LOW/NONE)
- `detection_basis` - Always SIGNAL_PROCESSING for VBI
- `observer_version` - Semantic version of observer

**Observer-Specific Data**:
- **BiphaseObservation**: `vbi_data[3]` (24-bit values for lines 16-18), optional `picture_number`, `clv_timecode`, `chapter_number`, `stop_code_present`
- **ClosedCaptionObservation**: `data0`, `data1` (7-bit characters), `parity_valid[2]`
- **VitcObservation**: timecode structure with hours/minutes/seconds/frames
- **VideoIdObservation**: aspect_ratio, cgms_a, aps flags
- **FmCodeObservation**: 20-bit data field
- **WhiteFlagObservation**: boolean flag status

**Validation**: Observers achieve 100% accuracy for biphase data and 84% for closed captions (validated against legacy ld-process-vbi tool across 3,256 fields).

VBI observations may be regenerated whenever the signal changes.

**Phase 2b Implementation**: Full observer framework implemented in `orc/core/{biphase,vitc,closed_caption,video_id,fm_code,white_flag}_observer.{h,cpp}` with shared utilities in `vbi_utilities.{h,cpp}`. Uses actual VideoParameters from TBC metadata for precise zero-crossing detection and includes 3-sample debounce filtering for noise rejection.

---

### 6.5 VITS Observations

Represents VITS measurements.

Fields:
- `FieldID`
- Metric type
- Measured value(s)
- Measurement confidence

VITS observations are purely descriptive.

**Phase 1 Implementation**: VITS metrics from TBC metadata are available via `TBCMetadataReader::read_vits_metrics()`. See Section 4.4 for the `VITSMetrics` structure. This will be used as hints for the Phase 2 VITS Observer.

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

**Phase 1 Implementation**: TBC metadata reading is fully implemented via `TBCMetadataReader`. See Section 4.4 for all available metadata structures. Metadata writing and new artifact persistence will be implemented in future phases.

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

- Exact sample storage format optimization
- Chunk size optimization for PCM/EFM
- Long-term cache eviction strategy
- Observer parallelism details
- Metadata write operations
- New artifact persistence format

These do not block initial implementation.

---

## 13. Implementation Status Summary

### Phase 1 (COMPLETED - December 2025)

**Core Data Structures**:
- ✅ `FieldID` class with full arithmetic and comparison operations
- ✅ `FieldIDRange` for representing field sequences
- ✅ `Artifact` base class with `ArtifactID` and `Provenance`
- ✅ `VideoFieldRepresentation` abstract interface
- ✅ `FieldDescriptor` for field metadata
- ✅ `VideoFormat` enum (PAL/NTSC)

**TBC I/O**:
- ✅ `TBCReader` for binary TBC file access with LRU caching
- ✅ `TBCMetadataReader` for SQLite metadata database reading
- ✅ `TBCVideoFieldRepresentation` concrete implementation
- ✅ All metadata structures (VideoParameters, FieldMetadata, DropoutRecord, VBIData, VITCData, PCMAudioParameters, VITSMetrics, ClosedCaptionData)

**Testing**:
- ✅ Unit tests for all core data structures
- ✅ Integration tests with 6 real TBC files (PAL/NTSC, CAV/CLV)
- ✅ 2,444 fields processed, ~914 MB test data validated

**Project System**:
- ✅ `Project` structure with sources, DAG nodes, edges
- ✅ `ProjectSource` (source_id, path, display_name)
- ✅ `ProjectDAGNode` (node_id, stage_name, node_type, display_name, x/y position, source_id, parameters)
- ✅ `ProjectDAGEdge` (source_node_id, target_node_id)
- ✅ Project file I/O (YAML format) via `project_io::load_project()` and `save_project()`
- ✅ Complete CRUD API for DAG manipulation:
  - `add_node()`, `remove_node()`, `change_node_type()`, `set_node_parameters()`, `set_node_position()`
  - `add_edge()`, `remove_edge()`
  - `add_source_to_project()`, `remove_source_from_project()`
  - `can_change_node_type()` validation
  - `clear_project()` reset
- ✅ Automatic modification tracking (is_modified flag) for all CRUD operations
- ✅ Source ID management and auto-generated node IDs
- ✅ Node type enumeration (SOURCE, SINK, TRANSFORM, SPLITTER, MERGER, COMPLEX)
- ✅ Parameter storage with type-safe variant (int32, uint32, double, bool, string)

**Source Files**:
- `orc/core/include/field_id.h` and `field_id.cpp`
- `orc/core/include/artifact.h` and `artifact.cpp`
- `orc/core/include/video_field_representation.h` and `video_field_representation.cpp`
- `orc/core/include/tbc_reader.h` and `tbc_reader.cpp`
- `orc/core/include/tbc_metadata.h` and `tbc_metadata.cpp`
- `orc/core/include/tbc_video_field_representation.h` and `tbc_video_field_representation.cpp`
- `orc/core/include/project.h` and `project.cpp`
- All test files in `orc/tests/`

### Phase 2 (Planned - Observer Framework)

**To Be Implemented**:
- Observer base class and framework
- Dropout Observer (using TBC dropout records as hints)
- VBI Observer (using TBC VBI data as hints)
- VITS Observer (using TBC VITS metrics as hints)
- Observation storage and caching
- Observer provenance tracking

### Future Phases

**To Be Implemented**:
- Decision artifact schema and storage
- Signal-transforming stages (dropout correction, stacking)
- PCM and EFM processing
- Metadata write operations
- Complete CLI and GUI tools

---

