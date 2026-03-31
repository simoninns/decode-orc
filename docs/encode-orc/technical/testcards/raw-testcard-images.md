# Testcard Raw Y'CbCr 4:2:2 Assets

The pal-raw and ntsc-raw folders contain raw **Y'CbCr 4:2:2** color bar frames for PAL EBU and NTSC EIA at 75% and 100% saturation. Files are single-frame images sized to the active picture area only.

## Format Specification

Raw video file containing **ITU-R BT.601-derived component video**:

### Video Standard
- **Y'CbCr 4:2:2** sampling structure
- **10-bit quantization**, packed as YUYV
- **Studio range** (ITU-R BT.601):
  - Y' (luma): 64–940 (876 quantization levels)
  - Cb/Cr (chroma): 64–960 (896 quantization levels, centered at 512)
- **Byte order**: Little-endian
- **Field order**: Top field first (interlaced)

### Active Picture Dimensions
- **PAL**: 720 × 576 @ 50i
- **NTSC**: 720 × 480 @ 59.94i

### Bit Packing
Each 10-bit component value is stored in a 16-bit (2-byte) little-endian word:
- **Bits 0–9**: 10-bit sample value (valid range)
- **Bits 10–15**: Unused (should be zero)

Example: A luma value of 940 (0x3AC) is stored as bytes `0xAC 0x03`

## Files
- `pal-ebu-colorbars-100.raw` — 720×576, PAL EBU bars, 100% saturation
- `pal-ebu-colorbars-75.raw` — 720×576, PAL EBU bars, 75% saturation
- `ntsc-eia-colorbars-100.raw` — 720×480, NTSC EIA/SMPTE bars, 100% saturation
- `ntsc-eia-colorbars-75.raw` — 720×480, NTSC EIA/SMPTE bars, 75% saturation

## YUYV Packing Format

Each pair of horizontally-adjacent pixels is encoded as 4 components (8 bytes total):

| Component | Description | Bytes | Applies to |
|-----------|-------------|-------|------------|
| **Y0** | Luma for pixel 0 | 0–1 | Pixel 0 |
| **Cb** (U) | Blue-difference chroma | 2–3 | Pixels 0 & 1 (shared) |
| **Y1** | Luma for pixel 1 | 4–5 | Pixel 1 |
| **Cr** (V) | Red-differenc (10-bit)

### Y' (Luma)
| Level | Value | Description |
|-------|-------|-------------|
| Black | 64 | Reference black (0% video) |
| White | 940 | Reference white (100% video) |
| Range | 876 levels | 64–940 inclusive |
| Headroom | Below 64, above 940 | Reserved for superblack/superwhite |

### Cb/Cr (Chroma)
| Level | Value | Description |
|-------|-------|-------------|
| Minimum | 64 | Maximum color difference (negative) |
| Neutral | 512 | Zero chroma (grayscale) |
| Maximum | 960 | Maximum color difference (positive) |
| Range | 896 levels | 64–960 inclusive, centered at 512 |

### RGB to Y'CbCr Conversion (BT.601)
```
Y'  = 0.299·R + 0.587·G + 0.114·B
Cb  = -0.168736·R - 0.331264·G + 0.5·B
Cr  = 0.5·R - 0.418688·G - 0.081312·B
```
Where R, G, B are normalized to 0.0–1.0, then scaled to studio range.   512       64        512

### Chroma Subsampling
4:2:2 subsampling means:
- Each pixel has its own **luma** (Y') sample
- Pairs of horizontal pixels **share** chroma (Cb/Cr) samples
- Chroma is averaged/interpolated for the pixel pair


### Calculation
- Each pixel pair = 4 components × 2 bytes = **8 bytes**
- Horizontal pixel pairs per line = width ÷ 2
- Total bytes = (width ÷ 2) × height × 8

### Actual Sizes
| Format | Dimensions | Calculation | Size |
|--------|------------|-------------|------|
| **PAL** | 720 × 576 | (720÷2) × 576 × 8 | **1,658,880 bytes** (1.58 MiB) |
| *Usage Notes

### Scope
- Files contain **active picture area only** (no blanking or VBI)
- Single frame per file (no field/frame sequencing)
- Top field first ordering (line 0 = top field, line 1 = bottom field, etc.)

### Applications
- Deterministic encoder test inputs with well-defined digital levels
- Y'CbCr domain validation and analysis
- Baseline reference for colorimetry testing
- ITU-R BT.601 compliance verification

### Important Considerations
1. **Field structure**: Data represents a full frame with interlaced field ordering
2. **Blanking**: Add horizontal/vertical blanking and VBI as needed for your application
3. **Timing**: Frame rate implied by format (50i for PAL, 59.94i for NTSC) but not encoded in file
4. **Color space**: Assumes ITU-R BT.601 color primaries and transfer characteristics

### Reading the Files
To read these files programmatically:
```python
import struct

def read_yuyv_frame(filename, width, height):
    with open(filename, 'rb') as f:
        data = f.read()
    
    # Each pixel pair = 8 bytes (4 × 16-bit components)
    expected_size = (width // 2) * height * 8
    assert len(data) == expected_size
    
    # Unpack as little-endian 16-bit unsigned integers
    components = struct.unpack(f'<{len(data)//2}H', data)
    
    # Components are in Y0, Cb, Y1, Cr order
    # Process in groups of 4...
```
- **Cb/Cr (Chroma)**:
  - Minimum: 64
  - Neutral: 512 (zero chroma)
  - Maximum: 960
  - Range: 896 levels

## Color Ordering (left to right)
White, Yellow, Cyan, Green, Magenta, Red, Blue, Black.

## File Sizes (bytes)
- **PAL**: 720 × 576 × 4 bytes per pixel pair = (720/2) × 576 × 4 × 2 = 1,658,880 bytes
- **NTSC**: 720 × 480 × 4 bytes per pixel pair = (720/2) × 480 × 4 × 2 = 1,382,400 bytes

## Generation
To regenerate these files, run:
```bash
python3 scripts/generate_pal_yuv422_colorbars.py
python3 scripts/generate_ntsc_yuv422_colorbars.py
```

## Notes
- Images contain only the active picture region; add your own blanking/VBI if needed.
- Use these as baseline deterministic inputs for encoder tests with well-defined digital levels.
- Chroma subsampling is 4:2:2: each pixel has independent luma, but chroma is horizontally shared between pixel pairs.
