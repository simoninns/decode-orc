# VFR Field Length Standards Compliance Refactor Plan

## Problem Statement

Currently, the VFR (Video Field Representation) system does not correctly represent the variable field lengths required by NTSC and PAL broadcast standards. This results in incorrect line counts:

### Current (Incorrect) Implementation
- **NTSC**: All fields are 263 lines → 526 lines per frame (should be 525)
- **PAL**: All fields are 313 lines → 626 lines per frame (should be 625)

### Required (Standards-Compliant) Implementation
- **NTSC**: 
  - Frame has 525 total lines
  - Field order: Even field (262 lines) then Odd field (263 lines)
  - Even field + Odd field = 262 + 263 = 525 lines ✓
  
- **PAL**:
  - Frame has 625 total lines
  - Field order: Odd field (312 lines) then Even field (313 lines)
  - Odd field + Even field = 312 + 313 = 625 lines ✓

### Additional Requirements
1. **Source TBCs** (external format) contain padding in the first field to make both fields equal length
   - This padding must be **removed** when source stage creates the VFR
   - Field length is represented in VFR via `FieldDescriptor.height` (per-field)

2. **ld-decode Sink** must add padding back when writing TBCs
   - Padding consists of blanking-level samples
   - This ensures compatibility with legacy tools

## Critical Architecture Principle

**The VFR is the boundary between external formats and orc-core's internal representation.**

```
External (TBC) → Source Stage → VFR → Processing Stages → Sink Stages → External (TBC)
                      ↑                                              ↑
              Reads TBC metadata                    Only LD Sink writes metadata
              Removes padding                       LD Sink adds padding back
```

- **TBC metadata** = External format (SQLite .tbc.db files from ld-decode)
- **VFR** = orc-core's internal representation (self-contained interface)
- **Only source stages** read TBC metadata (to determine field parity)
- **Only LD Sink stage** writes TBC metadata and handles padding
- **Other sink stages** do NOT write TBC files - they write to other formats (video, audio, etc.)
- **All processing stages** work exclusively with VFR interface

## Architecture Analysis

### VFR Boundary and Data Flow

```
┌──────────────────────────────────────────────────────────────┐
│                  EXTERNAL FORMAT (TBC)                       │
│  - Fixed-size fields (with padding in first field)          │
│  - TBC metadata database (.tbc.db)                          │
│  - Legacy ld-decode format                                  │
└─────────────────────────┬────────────────────────────────────┘
                          │
                          │ Read by Source Stage
                          ↓
┌──────────────────────────────────────────────────────────────┐
│               VFR (Video Field Representation)               │
│                   orc-core's Internal Format                 │
│                                                              │
│  - FieldDescriptor.height varies per field                  │
│  - No padding (standards-compliant line counts)             │
│  - Self-contained interface                                 │
│  - All stages communicate via VFR only                      │
└─────────────────────────┬────────────────────────────────────┘
                          │
                          │ Used by all processing stages
                          ↓
┌──────────────────────────────────────────────────────────────┐
│                  EXTERNAL FORMAT (TBC)                       │
│  - Padding added back by Sink Stage                         │
│  - TBC metadata written                                     │
└──────────────────────────────────────────────────────────────┘
```

### Current System Components

#### 1. SourceParameters (`orc/view-types/orc_source_parameters.h`)
```cpp
struct SourceParameters {
    int32_t field_width = -1;   // Samples per line (horizontal)
    int32_t field_height = -1;  // Lines per field (vertical) - CURRENTLY CONSTANT
    // ... other fields
};
```

**Issue**: `field_height` is currently a single value applied to all fields.
**Note**: This is populated from TBC metadata by source stages, but **should not be used directly** for per-field heights.

#### 2. FieldDescriptor (`orc/core/include/video_field_representation.h`) - **VFR INTERFACE**
```cpp
struct FieldDescriptor {
    FieldID field_id;
    FieldParity parity;
    VideoFormat format;
    size_t width;   // Samples per line
    size_t height;  // Number of lines - MUST VARY PER FIELD
    // ... optional fields
};
```

**Issue**: `hCore VFR Interface Changes

#### 1.1 Implement Standards-Based Field Height Calculation
**New Utility Function** (in `orc/core/include/video_field_representation.h` or similar):

```cpp
namespace orc {

/**
 * @brief Calculate standards-compliant field height
 * 
 * @param system Video system (NTSC, PAL, etc.)
 * @param is_first_field True if this is the first field in temporal order
 * @return Number of lines in this field (no padding)
 */
inline size_t calculate_standard_field_height(VideoSystem system, bool is_first_field) {
    if (system == VideoSystem::NTSC || system == VideoSystem::PAL_M) {
        // NTSC: Even field (first) = 262 lines, Odd field (second) = 263 lines
        return is_first_field ? 262 : 263;
    } else if (system == VideoSystem::PAL) {
        // PAL: Odd field (first) = 312 lines, Even field (second) = 313 lines
        return is_first_field ? 312 : 313;
    }
    // Unknown system - should not happen
    return 0;
}

/**
 * @brief Calculate padded field height (TBC file format)
 * Used only by LD Sink stage when writing TBC files for ld-decode compatibility
 */
inline size_t calculate_padded_field_height(VideoSystem system) {
    if (system == ViVFR Implementations (Source Stages)

#### 2.1 Modify get_descriptor() to Calculate Per-Field Heights
**Files**:
- `orc/core/tbc_video_field_representation.cpp`
- `orc/core/tbc_yc_video_field_representation.cpp`

**Current Logic**:
```cpp
std::optional<FieldDescriptor> get_descriptor(FieldID id) const {
    FieldDescriptor desc;
    desc.height = video_params_.field_height;  // WRONG: constant for all fields
    // ...
}
```

**New Logic**:
```cpp
std::optional<FieldDescriptor> get_descriptor(FieldID id) const {
    FieldDescriptor desc;
    
    // Determine field parity from TBC metadata (via field parity hint)
    bool is_first_field = false;
    auto parity_hint = get_field_parity_hint(id);
    if (parity_hint.has_value()) {
        is_first_field = parity_hint->is_first_field;
    } else {
        // Fallback: infer from field ID
        is_first_field = (id.value() % 2 == 0);
        ORC_LOG_WARN("No parity hint for field {}, using ID-based inference", id.value());
    }
    
    // Calculate standards-compliant height (VFR representation - no padding)
    desc.height = calculate_standard_field_height(video_params_.system, is_first_field);
    
    // ... rest of descriptor
}
```

**Note**: `get_field_parity_hint()` reads from TBC metadata (which was loaded by source stage). This is the ONLY place TBC metadata is consulted after VFR creation

#### 2.1 Modify get_descriptor() to Use Per-Field Line Count
**Files**:
- `orc/core/tbc_video_field_representation.cpp`
- `orc/core/tbc_yc_video_field_representation.cpp`

**Current Logic**:
```cpp
std::optional<FieldDescriptor> get_descriptor(FieldID id) const {
    FieldDescriptor desc;
    desc.height = video_params_.field_height;  // WRONG: constant for all fields
    // ...
}
```

**New Logic**:
```cpp
std::optional<FieldDescriptor> get_descriptor(FieldID id) const {
    FieldDescriptor desc;
    
    // Try to get per-field line count from metadata
    auto metadata = get_field_metadata(id);
    if (metadata && metadata->field_line_count.has_value()) {
        desc.height = metadata->field_line_count.value();
    } else {
        // Fallback to calculating from standards
        desc.height = calculate_standard_field_height(id, video_params_.system);
    }
    // ...
}
```

#### 2.2 Implement Standard Field Height Calculation
**New Utility Function** (in appropriate header/cpp):

```cpp
size_t calculate_standard_field_height(FieldID field_id, VideoSystem system, bool is_first_field) {
    if (system == VideoSystem::NTSC || system == VideoSystem::PAL_M) {
        // NTSC: Even field (first) = 262 lines, Odd field (second) = 263 lines
        return is_first_field ? 262 : 263;
    } else if (system == VideoSystem::PAL) {
        // PAL: Odd field (first) = 312 lines, Even field (second) = 313 lines
        return is_first_field ? 312 : 313;
    }
    // Fallback - should not happen
    return 0;
}
```

**Note**: This functTBC Reading (Source VFR Implementations)

#### 3.1 Remove Padding When Reading TBC Files
**Files**:
- `orc/core/tbc_video_field_representation.cpp`
- `orc/core/tbc_yc_video_field_representation.cpp`

**Current Behavior**: 
- TBC reader reads all lines from file (including padding)
- All fields reported as same height

**New Behavior**:
1. Read full field from TBC file (with padding)
2. Determine actual line count from field parity and video system
3. Truncate to standards-compliant line count
4. Return via VFR interface with correct `FieldDescriptor.height`

**Implementation Approach**:

```cpp
// In TBCVideoFieldRepresentation or TBCYCVideoFieldRepresentation
const sample_type* get_line(FieldID id, size_t line) const {
    // Get field parity to determine actual height
    bool is_first_field = false;
    auto parity_hint = get_field_parity_hint(id);
    if (parity_hint.has_value()) {
        is_first_field = parity_hint->is_first_field;
    }
    
    // Calculate standards-compliant height (VFR representation)
    size_t actual_height = calculate_standard_field_height(video_params_.system, is_first_field);
    
    // Validate line number against actual height
    if (line >= actual_height) {
        return nullptr;  // Out of bounds for VFR representation
    }
    
    // Read from TBC file (which may have padding, but we only expose actual lines)
    // ... existing TBC read logic
}

std::vector<sample_type> get_field(FieldID id) const {
    // Determine actual line count
    bool is_first_field = false;
    auto parity_hint = get_field_parity_hint(id);
    if (parity_hint.has_value()) {
        is_first_field = parity_h (VFR to TBC Conversion)

#### 4.1 Add Padding When Writing TBC Files
**File**: `orc/core/stages/ld_sink/ld_sink_stage.cpp`

**Current Behavior**:
```cpp
// Accumulate all lines of the field into buffer
for (size_t line_num = 0; line_num < expected_lines; ++line_num) {
    const uint16_t* line_data = representation->get_line(field_id, line_num);
    field_buffer.insert(field_buffer.end(), line_data, line_data + line_width);
}
// Write the entire field to TBC
tbc_writer.write(field_buffer);
```

**New Behavior**:
```cpp
// Get field descriptor from VFR (contains actual line count)
auto descriptor = representation->get_descriptor(field_id);
if (!descriptor) {
    ORC_LOG_ERROR("No descriptor for field {}", field_id.value());
    continue;
}

size_t actual_lines = descriptor->height;  // VFR's standards-compliant height
size_t line_width = descriptor->width;

// Get field parity to determine if padding needed
auto parity_hint = representation->get_field_parity_hint(field_id);
bool is_first_field = parity_hint.has_value() && parity_hint->is_first_field;

// Calculate padded height for TBC file format
auto video_params = representation->get_video_parameters();
size_t padded_lines = calculate_padded_field_height(video_params->system);

// Accumulate all lines from VFR
std::vector<uint16_t> field_buffer;
field_buffer.reserve(padded_lines * line_width);

for (size_t line_num = 0; line_num < actual_lines; ++line_num) {
    const uint16_t* line_data = representation->get_line(field_id, line_num);
    if (!line_data) {
        ORC_LOG_WARN("Field {} line {} has no data", field_id.value(), line_num);
        field_buffer.insert(field_buffer.end(), line_width, 0);
    } else {
        field_buffer.insert(field_buffer.end(), line_data, line_data + line_width);
    }
}

// Add padding for first field if needed (TBC file format requirement)
if (is_first_field && actual_lines < padded_lines) {
    size_t padding_lines = padded_lines - actual_lines;
    uint16_t blanking_level = video_params->blanking_16b_ire;
    
    ORC_LOG_DEBUG("Adding {} padding lines to first field {} (blanking level {})", 
                  padding_lines, field_id.value(), blanking_level);
    
    // Add blanking-level padding lines at end
    for (size_t i = 0; i < padding_lines; ++i) {
        field_buffer.insert(field_buffer.end(), line_width, blanking_level);
    }
}

// Write the TBC field (with padding if first field)
tbc_writer.write(field_buffer);
```

**Note**: The sink reads from VFR (which has no padding) and writes to TBC (which requires padding). This is the inverse of what the source does.Add padding for first field if needed
if (is_first_field && actual_lines < padded_lines) {
    size_t padding_lines = padded_lines - actual_lines;
    uint16_t blanking_level = video_params.blanking_16b_ire;
    
    // Add blanking-level padding lines
    for (size_t i = 0; i < padding_lines; ++i) {
        field_buffer.insert(fi
**Scenario**: TBC files from old ld-decode that don't have field parity in metadata.

**Mitigation**:
1. If `get_field_parity_hint()` returns no value, infer from field ID:
   ```cpp
   // Fallback: assume standard alternating pattern
   bool is_first_field = (field_id.value() % 2 == 0);
   ```
2. Log warning about missing parity information
3. Calculate field height using inferred parity
4. VFR still provides correct interface to processing stages

**Note**: Most TBC files from ld-decode DO have `is_first_field` in metadata, so this is a rare case.count:

```cpp
FieldMetadata field_meta;
field_meta.seq_no = field_id.value() + 1;
field_meta.is_first_field = parity_hint->is_first_field;
field_meta.field_line_count = actual_lines;  // NEW: Store actual line count
// ... rest of metadata
metadata_writer.write_field_metadata(field_meta);
```

## Breaking Changes and Compatibility

### This is a Breaking Change

**Important**: This refactor introduces **breaking changes** to the VFR representation. There is **no requirement for backward compatibility** with projects or workflows created before this refactor.

**What breaks**:
1. **VFR field heights change**: Fields will have different heights (262/263 for NTSC, 312/313 for PAL)
2. **Line numbering changes**: Maximum line numbers differ between first and second fields
3. **Frame assembly**: Frame rendering must handle asymmetric field heights
4. **Existing analysis data**: May reference line numbers that no longer exist in shorter fields

**User Impact**:
- **Projects created before this refactor** may produce different results or fail validation
- **Workflows expecting 263/313 line fields** will see different field heights
- **Custom stages** that hardcode field heights will need updates
- **Analysis/observation data** stored with old field heights may be invalid

**Migration Path**:
- Users should **re-import** source TBC files after upgrade
- Existing orc project files (.orcprj) may need regeneration
- No automatic migration of old projects is provided

## Implementation Phases

### Phase 1: Core VFR Interface Changes (Low Risk)
**Objective**: Implement the standards-based field height calculation utilities

**Tasks**:
1. Implement `calculate_standard_field_height()` inline function
   - Takes VideoSystem and is_first_field
   - Returns standards-compliant line count (262/263 for NTSC, 312/313 for PAL)
2. Implement `calculate_padded_field_height()` inline function
   - Takes VideoSystem
   - Returns padded height used by TBC files (263 for NTSC, 313 for PAL)
3. Add unit tests for both functions
4. Document the VFR boundary architecture principle

**Duration**: 2-3 days

**Acceptance Criteria**:
- ✓ Utilities compile and pass unit tests
- ✓ All field height calculations are correct
- ✓ Code review complete

---

### Phase 2: Update VFR Implementations - get_descriptor() (Medium Risk)
**Objective**: Make `FieldDescriptor.height` vary per field based on parity

**Files**:
- `orc/core/tbc_video_field_representation.cpp`
- `orc/core/tbc_yc_video_field_representation.cpp`

**Tasks**:
1. Update `TBCVideoFieldRepresentation::get_descriptor()` to:
   - Get field parity hint from TBC metadata
   - Use `calculate_standard_field_height()` to determine correct height
   - Return descriptor with standards-compliant height
2. Update `TBCYCVideoFieldRepresentation::get_descriptor()` identically
3. Add fallback to field ID-based parity inference if hint unavailable
4. Test that descriptor heights vary correctly between fields
5. Verify processing stages see correct heights via VFR interface

**Duration**: 3-4 days

**Acceptance Criteria**:
- ✓ Descriptor height differs for first/second fields
- ✓ NTSC: even fields = 262, odd fields = 263
- ✓ PAL: odd fields = 312, even fields = 313
- ✓ All VFR interface tests pass
- ✓ Code review complete

---

### Phase 3: Update VFR Implementations - Field Reading (Medium Risk)
**Objective**: Remove TBC padding from VFR representation

**Files**:
- `orc/core/tbc_video_field_representation.cpp`
- `orc/core/tbc_yc_video_field_representation.cpp`

**Tasks**:
1. Update `get_line()` method to:
   - Validate line number against standards-compliant height (not padded height)
   - Return `nullptr` for lines beyond standards-compliant height
   - Read from TBC file (which may have padding, but truncate)
2. Update `get_field()` method to:
   - Calculate standards-compliant height
   - Return only actual lines (ignore padding in TBC file)
3. Test field reading with both composite (TBC) and Y/C sources
4. Verify TBC padding is correctly hidden from VFR consumers
5. Validate that line 0 through (actual_height-1) are accessible

**Duration**: 2-3 days

**Acceptance Criteria**:
- ✓ Field data returned has correct length (262/263 or 312/313)
- ✓ Accessing lines beyond actual height returns nullptr
- ✓ Both composite and Y/C implementations work correctly
- ✓ TBC file padding is transparent to VFR interface
- ✓ All field reading tests pass
- ✓ Code review complete

---

### Phase 4: Update LD Sink Stage (Medium Risk)
**Objective**: Add padding when writing TBC files

**Files**:
- `orc/core/stages/ld_sink/ld_sink_stage.cpp` (LD Sink ONLY)

**Note**: Only LD Sink stage writes TBC files. Other sink stages (Chroma, Audio, EFM, analysis sinks) are unaffected.

**Tasks**:
1. Update `LDSinkStage::write_tbc_and_metadata()` to:
   - Get field descriptor from VFR (contains actual line count)
   - Determine if field is first or second from parity hint
   - Calculate padded height needed for TBC file format
   - Read actual lines from VFR
   - Add blanking-level padding lines for first field
   - Write complete padded field to TBC file
2. Validate padding calculation is correct
3. Test round-trip: Load TBC → Export via LD Sink → Load again
4. Verify exported TBC is compatible with ld-decode tools

**Duration**: 2-3 days

**Acceptance Criteria**:
- ✓ LD Sink writes correct padding (1 line for NTSC, 1 line for PAL first field)
- ✓ Padding uses blanking_16b_ire level
- ✓ TBC files are valid and readable by ld-decode
- ✓ Round-trip produces standards-compliant output
- ✓ Only first field of each frame is padded
- ✓ Code review complete

---

### Phase 5: Component Updates and Testing (Low-Medium Risk)
**Objective**: Ensure all orc-core components work with variable field heights

**Tasks**:
1. Update preview rendering:
   - Adjust frame buffer sizing for asymmetric field heights
   - Update line mapping calculations
   - Test frame preview rendering
2. Update line scope and analysis tools:
   - Update line number validation
   - Display actual field line count in UI
   - Update documentation
3. Verify all observers work correctly:
   - FM Code Observer (NTSC line 10)
   - White Flag Observer (NTSC line 11)
   - VITS observers (both formats)
   - All other observers
4. Create comprehensive test suite:
   - Unit tests for all utility functions
   - Integration tests for VFR implementations
   - Round-trip tests (load → export → load)
5. Test with legacy tools:
   - Verify ld-chroma-decoder compatibility
   - Verify ld-process-vits compatibility
6. Update user documentation

**Duration**: 3-4 days

**Acceptance Criteria**:
- ✓ Preview rendering works with variable field heights
- ✓ All observers function correctly
- ✓ All new tests pass
- ✓ Legacy tool compatibility verified
- ✓ Frame counts match standards (525 NTSC, 625 PAL)
- ✓ Documentation updated
- ✓ Code review complete

---

## Timeline Summary

- **Phase 1**: 2-3 days (Core utilities and tests)
- **Phase 2**: 3-4 days (get_descriptor() updates)
- **Phase 3**: 2-3 days (Field reading updates)
- **Phase 4**: 2-3 days (LD Sink updates)
- **Phase 5**: 3-4 days (Component updates and testing)

**Total**: ~12-17 days (2.5-3.5 weeks)
- Clear error logging when this occurs
- Validation in debug builds

## References

- NTSC Standard: 525 total lines per frame (even: 262, odd: 263)
- PAL Standard: 625 total lines per frame (odd: 312, even: 313)
- ld-decode legacy implementation: `legacy-tools/library/tbc/lddecodemetadata.cpp`
- Current line numbering documentation: `docs-user/wiki-default/line-numbering.md`

## Notes

### Field Terminology Clarity
- **First field**: The field that appears first in time
  - NTSC: Even field (starts on whole line, 262 lines)
  - PAL: Odd field (starts on half line, 312 lines)
- **Second field**: The field that appears second in time
  - NTSC: Odd field (starts on half line, 263 lines)
  - PAL: Even field (starts on whole line, 313 lines)

### Padding Location
- Padding is ALWAYS added to the **first field** (in temporal order)
- This makes both fields equal length in TBC files
- Padding consists of lines at **blanking level** (`blanking_16b_ire`)
- Padding is added at the **end** of the field (after last active line)
- **Important**: Padding addition is ONLY relevant to LD Sink stage

### Other Sink Stages
- **Chroma Sink**: Writes decoded video (RGB/YCbCr) - no TBC format concerns
- **Audio Sink**: Writes PCM audio - no VFR field height concerns
- **EFM Sink**: Writes EFM data - no VFR field height concerns
- **Other Analysis Sinks**: Write analysis data - no TBC format concerns
- **None of these need modifications** for this refactor
- **Important**: Padding addition is ONLY relevant to LD Sink stage

### Other Sink Stages
- **Chroma Sink**: Writes decoded video (RGB/YCbCr) - no TBC format concerns
- **Audio Sink**: Writes PCM audio - no VFR field height concerns
- **EFM Sink**: Writes EFM data - no VFR field height concerns
- **Other Analysis Sinks**: Write analysis data - no TBC format concerns
- **None of these need modifications** for this refactor