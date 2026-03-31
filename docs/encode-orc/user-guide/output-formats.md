# Output Formats

encode-orc generates video files in two main output types: TBC (Time Base Corrected) format with metadata, or Standard format without metadata.

## Output Types

### TBC Format (Default)

TBC files are field-based video files with metadata database support, compatible with decode-orc and other video decoding tools. Each field is stored sequentially without compression.

**Features:**
- Includes SQLite metadata database (.tbc.db)
- VBI data support
- Frame/field numbering and timecode
- Dropout detection metadata
- Compatible with decode-orc workflow

**Usage:**
```yaml
output:
  type: "tbc"  # or omit, as TBC is default
```

### Standard Format

Standard format outputs raw 16-bit signed field data without any metadata or padding. Fields are written exactly as generated with asymmetric sizes.

**Features:**
- Raw 16-bit signed samples (little-endian)
- No metadata database
- No padding between fields
- Smaller file sizes
- Useful for direct video processing tools

**Usage:**
```yaml
output:
  type: "standard"
```

## Video Modes

Both TBC and Standard output types support two video modes:

### File Specifications

| Aspect | TBC Format | Standard Format |
|--------|-----------|-----------------|
| Bit Depth | Unsigned 16-bit | 16-bit signed |
| Byte Order | Little-endian | Little-endian |
| Field Order | Top field first | Top field first |
| Metadata | SQLite database | None |
| Padding | Yes (symmetric fields) | No (asymmetric fields) |
| Compression | None | None |

## Composite Mode

Generates traditional composite video representation.

### Output Files

**TBC Format:**
- `output.tbc` - Composite field-based video file (unsigned 16-bit)
- `output.tbc.db` - SQLite metadata database

**Standard Format:**
- `output.tbc` - Composite field-based video file (16-bit signed)

### File Structure

**TBC File:**
- Stores composite video as unsigned 16-bit grayscale
- Each field stored sequentially
- Two bytes per pixel (little-endian)

**Metadata Database:**
- VBI (Vertical Blanking Interval) data
- Frame/field numbering
- Timecode information
- Dropout detection data

### Characteristics
- Single file containing video
- Simpler file organization
- Smaller file sizes than Y/C mode
- type: "tbc"      # or "standard"
  format: "composite"

## Y/C Mode (VHS, S-VHS, etc.)

Separates luma (brightness) and chroma (color) components, simulating outputs such as S-VHS.

### Output Files

**TBC Format:**
- `output.tbcy` - Luma (Y) component (unsigned 16-bit)
- `output.tbcc` - Chroma (C) component (unsigned 16-bit)
- `output.tbc.db` - SQLite metadata database

**Standard Format:**
- `output.tbcy` - Luma (Y) component (16-bit signed)
- `output.tbcc` - Chroma (C) component (16-bit signed)

### Output Files
- `output.tbcy` - Luma (Y) component
- `output.tbcc` - Chroma (C) component
- `output.tbc.db` - SQLite metadata database

### File Structure

**Luma File (.tbcy):**
- Unsigned 16-bit grayscale image data
- Full resolution (720×480 or 720×576)
- Higher quality brightness information

**Chroma File (.tbcc):**
- Unsigned 16-bit chroma data
- 4:2:2 chroma subsampling
- Color information separate from brightness

**Metadata Database:**
- Same as composite mode
- Applies to both luma and chroma

### Characteristics
- Better separation of color components
- type: "tbc"      # or "standard"
  format: "yc"
```

## Metadata Database Format (TBC Only)
### Usage
```yaml
output:
  format: "yc"
```