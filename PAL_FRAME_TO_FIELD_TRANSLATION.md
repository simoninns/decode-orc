# PAL Frame Line to Field Parity and Line Translation

## Overview

The decode-orc project uses a systematic approach to translate PAL frame line numbers to field parity (top/bottom) and field line numbers. This is crucial because video is stored and processed in interlaced format with two fields per frame.

## Key Concepts

### Frame vs Field Lines

In PAL, frame lines are divided into two fields:

- **Frame**: 625 total scan lines in PAL
- **Frame line numbering** (1-based): 1, 2, 3, ..., 625
- **Field structure**:
  - **First Field (Odd field)**: Frame lines 1-313 (313 lines)
  - **Second Field (Even field)**: Frame lines 314-625 (312 lines)
- **Display interlacing pattern**: When displayed, the two fields are interleaved: 1, 314, 2, 315, 3, 316, ..., 313, 625

### Field Parity

- **First Field (Odd field)**: `FieldParity::Top` - contains frame lines 1-313
- **Second Field (Even field)**: `FieldParity::Bottom` - contains frame lines 314-625

In frame display, these two fields are interleaved: frame line 1 (field 1), frame line 314 (field 2), frame line 2 (field 1), frame line 315 (field 2), etc.

### PAL Half-Line Offset

In decode-orc/ld-decode format with 4fSC sampling, the two fields are stored sequentially. The `getOffset()` function tracks which field is being processed:

```cpp
int32_t getOffset() const {
    return (field.is_first_field.value_or(true)) ? 0 : 1;
}
```

- If `is_first_field = true`: offset = 0 (first field, frame lines 1-313)
- If `is_first_field = false`: offset = 1 (second field, frame lines 314-625)

The offset is used to adjust calculations when the active area boundaries span both fields.

## Translation Algorithm

### From Frame Line to Field Line

The translation is straightforward based on which field the frame line belongs to:

```cpp
if (frameLine >= 1 && frameLine <= 313) {
    // First field (odd field)
    parity = FieldParity::Top;
    fieldLine = frameLine;
} else if (frameLine >= 314 && frameLine <= 625) {
    // Second field (even field)
    parity = FieldParity::Bottom;
    fieldLine = frameLine - 313;
}
```

Or more concisely:
```cpp
FieldParity parity = (frameLine <= 313) ? FieldParity::Top : FieldParity::Bottom;
int32_t fieldLine = (frameLine <= 313) ? frameLine : (frameLine - 313);
```

### Inverse: From Field Line to Frame Line

To reconstruct the frame line number from field line and parity:

```cpp
if (parity == FieldParity::Top) {
    // First field (frame lines 1-313)
    frameLine = fieldLine;
} else {
    // Second field (frame lines 314-625)
    frameLine = fieldLine + 313;
}
```

### Using `getOffset()` and `is_first_field`

The `SourceField` struct wraps this logic:

```cpp
int32_t offset = inputField.getOffset();  // 0 for first field, 1 for second field

// Convert frame-based active area limits to field-based coordinates
const int32_t firstFieldLine = (videoParameters.first_active_frame_line + 1 - offset) / 2;
const int32_t lastFieldLine = (videoParameters.last_active_frame_line + 1 - offset) / 2;
```

The `+1` and division by 2 handle the interleaving of the two fields in the display structure.

## Implementation Details in decode-orc

### SourceField Structure

The `SourceField` struct contains the field metadata and encapsulates the offset logic:

```cpp
struct SourceField {
    orc::FieldMetadata field;           // Contains is_first_field flag
    std::vector<uint16_t> data;         // Composite video data
    std::vector<uint16_t> luma_data;    // YC: Y component
    std::vector<uint16_t> chroma_data;  // YC: C component
    bool is_yc;                         // True for YC sources
    
    // Return vertical offset: 0 for first field, 1 for second field
    int32_t getOffset() const {
        return (field.is_first_field.value_or(true)) ? 0 : 1;
    }
};
```

### Field Parity Hint

Field parity information comes from upstream metadata (e.g., ld-decode):

```cpp
struct FieldParityHint {
    /**
     * Whether this is the first field in an interlaced pair
     * - NTSC: first field = Field 1 (starts on whole line)
     * - PAL:  first field = Field 1 (starts on half line)  ← PAL offset
     */
    bool is_first_field;
    HintSource source;
    int confidence_pct;
};
```

## Practical Examples

### Example 1: Frame Line 19

**Given:**
- Frame line 19 (within 1-313 range)

**Calculation:**
```
frameLine = 19
parity = Top (first field)
fieldLine = 19
```

**Result:** Field line 19, Top field (First field)

### Example 2: Frame Line 332

**Given:**
- Frame line 332 (within 314-625 range)

**Calculation:**
```
frameLine = 332
parity = Bottom (second field)
fieldLine = 332 - 313 = 19
```

**Result:** Field line 19, Bottom field (Second field)

Note: Frame lines 19 and 332 are displayed consecutively (with 314 between them), but both represent field line 19 in their respective fields.

### Example 3: PAL Sequential Frame Display

The display pattern for frame lines around line 19-20:

| Display Position | Frame Line | Field | Field Line | Notes |
|-----------------|------------|-------|------------|-------|
| 37 | 19 | Top | 19 | First field |
| 38 | 332 | Bottom | 19 | Second field (332 - 313 = 19) |
| 39 | 20 | Top | 20 | First field |
| 40 | 333 | Bottom | 20 | Second field (333 - 313 = 20) |

### Example 4: Active Area (PAL typical)

**Given:**
- `first_active_frame_line = 45`
- `last_active_frame_line = 623`
- `is_first_field = true`

**Calculation:**
```
offset = 0 (first field)
firstFieldLine = (45 + 1 - 0) / 2 = 23
lastFieldLine = (623 + 1 - 0) / 2 = 312
```

**Result:** Field lines 23-312 in the first field are active

**For second field:**
```
offset = 1 (second field)
firstFieldLine = (45 + 1 - 1) / 2 = 22.5 → 22
lastFieldLine = (623 + 1 - 1) / 2 = 311.5 → 311
```

**Result:** Field lines 22-311 in the second field are active (accounts for interleaving)

## Where This Is Used

### 1. **Chroma Sink Decoder** (`palcolour.cpp`)

When decoding PAL fields, the decoder converts frame-based line limits to field-based:

```cpp
// Line 295-296
const int32_t firstLine = (videoParameters.first_active_frame_line + 1 - inputField.getOffset()) / 2;
const int32_t lastLine = (videoParameters.last_active_frame_line + 1 - inputField.getOffset()) / 2;

// Line 345: Convert field line back to frame line for output
const int32_t absoluteLineNumber = (fieldLine * 2) + inputField.getOffset();
```

### 2. **Transform PAL Filters** (`transformpal2d.cpp`, `transformpal3d.cpp`)

FFT-based filters convert frame-based tile positions to field-based coordinates:

```cpp
// transformpal2d.cpp, line 117-118
const int32_t firstFieldLine = (videoParameters.first_active_frame_line + 1 - inputField.getOffset()) / 2;
const int32_t lastFieldLine = (videoParameters.last_active_frame_line + 1 - inputField.getOffset()) / 2;
```

### 3. **Metadata and TBC Files**

The TBC (Time-Base Corrected) metadata stores video parameters with frame-line numbering, which must be converted to field-line numbering for processing:

```cpp
// tbc_metadata.cpp, line 241-242
params.first_active_field_line = params.first_active_frame_line / 2;
params.last_active_field_line = params.last_active_frame_line / 2;
```

## Key Characteristics of PAL Interlacing

1. **Two fields per frame**:
   - First field (odd): Frame lines 1-313 (313 lines)
   - Second field (even): Frame lines 314-625 (312 lines)
   - Total: 625 lines

2. **Display interlacing**:
   - When displayed, the two fields are interleaved
   - Pattern: 1 (field 1), 314 (field 2), 2 (field 1), 315 (field 2), ..., 313 (field 1), 625 (field 2)
   - This interleaving is accomplished through the active area formula: `(frameLineNumber + 1 - offset) / 2`

3. **Direct field line translation**:
   - First field (frame lines 1-313): field line = frame line (no conversion needed)
   - Second field (frame lines 314-625): field line = frame line - 313

4. **1-based numbering**:
   - Video frame lines use 1-based indexing (1-625)
   - Field lines are numbered starting from 1 (1-313 for each field)
   - The `+1` in active area calculations handles the 1-based frame numbering

## Summary

The translation from PAL frame line to field parity and line is straightforward:

1. **Determine field from frame line number**:
   - If frame line 1-313: First field (Top) / `is_first_field = true`
   - If frame line 314-625: Second field (Bottom) / `is_first_field = false`

2. **Calculate field line**:
   - For first field: `fieldLine = frameLine`
   - For second field: `fieldLine = frameLine - 313`

3. **Active area conversion** accounts for display interleaving: `(frameLineNumber + 1 - offset) / 2`
   - Where `offset = 0` for first field, `offset = 1` for second field

4. **Inverse operation** reconstructs the frame line:
   - For first field: `frameLine = fieldLine`
   - For second field: `frameLine = fieldLine + 313`

### The Display Interlacing Pattern

PAL frame display interleaves the two fields:
- **Frame position 1**: Frame line 1 (first field line 1)
- **Frame position 2**: Frame line 314 (second field line 1)
- **Frame position 3**: Frame line 2 (first field line 2)
- **Frame position 4**: Frame line 315 (second field line 2)
- ... and so on until frame position 625

This creates the pattern: 1, 314, 2, 315, 3, 316, ..., 313, 625 as mentioned in the documentation.
