# Overview

encode-orc is a PAL and NTSC video encoder that generates synthetic test data in TBC (Time Base Corrected) format. It simulates the output of RF decoding applications like ld-decode and vhs-decode, allowing you to create reproducible test cases for video decoding pipelines without requiring physical hardware or RF captures.

Important Note: encode-orc is a test tool.  It is not required for general use of decode-orc.

## Core Workflow

1. **Define project** - Create a YAML configuration file specifying your video parameters
2. **Prepare source media** - Provide input images or videos (PNG, YUV422, MOV, or MP4)
3. **Run encoder** - Process through the encoding pipeline
4. **Get outputs** - Receive TBC files and SQLite metadata database
5. **Test decoders** - Feed generated files into decode-orc or similar tools

## The Encoding Pipeline

encode-orc implements a sophisticated multi-stage pipeline to transform raw video data into authentic-looking TBC output. Each stage handles specific aspects of video encoding:

### Stage 1: Input Loading

Converts source media into a standardized YUV color space ready for processing.

**Supported Input Formats:**
- **PNG** - Lossless images (single frames or image sequences)
- **YUV422** - Raw uncompressed video (10-bit or 8-bit per component)
- **MOV** - QuickTime containers with H.264 or ProRes
- **MP4** - MPEG-4 containers with H.264 or other codecs

All inputs are internally normalized to YUV444P16 (16-bit per component) for maximum precision during encoding.

Note that MOV and YUV422 raw formats support "studio colour" that allows sub-blacks and over-whites required for testcards (for example a PLUGE).  MP4 and PNG are just for including 'normal' pictures and video into the output but are not accurate and should not be used for actual testcards.

### Stage 2: Field Splitting

Converts progressive frames into interlaced fields, a fundamental characteristic of analog video systems.

- Splits each frame into two fields: Field 1 (odd lines) and Field 2 (even lines)
- Maintains proper aspect ratio and timing for PAL (625i) or NTSC (480i) output
- Preserves color and luma information across the split

### Stage 3: Field Structure Generation

Constructs the complete field structure including synchronization signals and blanking regions.

**Components Added:**
- **Horizontal sync pulses** - Time the electron beam horizontal position
- **Vertical sync pulses** - Mark field boundaries
- **Blanking intervals** - Non-visible areas (top, bottom, left, right edges)
- **Color burst** - Subcarrier reference signal for color decoders
- **Vertical interval timing codes (VITC)** - Frame number and timecode information
- **Vertical blanking interval (VBI) data** - Metadata embedded in non-visible lines

### Stage 4: Metadata Generation

Embeds timing, content, and diagnostic information into the video signal.

**Available Metadata Generators:**
- **VITC Generator** - Embeds frame/field numbers and timecode
- **VITS Generator** - PAL/NTSC specific test signals in VBI
- **VBI Data Generator** - Custom data in vertical blanking interval
- **Biphase VBI Generator** - Digital data encoding for LaserDisc/VHS preservation

### Stage 5: Active Video Encoding

Transforms YUV video data into composite or Y/C (S-Video) signals with accurate color modulation.

**Encoding Systems:**

**PAL (625 lines @ 50Hz)**
- Composite output or Y/C (S-Video) output
- Correct color subcarrier frequency (4.43 MHz)
- Proper sync timing and blanking intervals
- Support for both standard and EBU color ranges

**NTSC (480 lines @ 59.94Hz)**
- Composite output or Y/C (S-Video) output
- Correct color subcarrier frequency (3.58 MHz)
- Phase alternation for chroma modulation
- Support for both standard and SMPTE color ranges

The encoder applies chroma and luma filtering to remove high-frequency artifacts and create realistic encoded output. Both output formats (composite and Y/C) use accurate subcarrier modulation based on the video standard.

### Stage 6: Field Preprocessing

Optional filtering stage to condition the encoded signal.

**Available Filters:**
- **Chroma low-pass filter** - Reduces high-frequency chroma artifacts
- **Luma low-pass filter** - Smooths luma transitions
- Custom filter chains via YAML configuration

### Stage 7: Field Effects

Simulates artifacts and signal degradation commonly found in analog recordings.

**Available Effects:**
- **Noise generator** - White or colored Gaussian noise at configurable SNR
- **Dropout simulator** - Simulates missing or corrupted data (dropouts)

Effects are applied after active encoding, allowing realistic simulation of playback degradation and mechanical artifacts.

## Output Formats

### TBC (Time Base Corrected)

**Composite TBC:**
- Raw video samples in 8-bit or 16-bit format
- Single channel containing composite video signal
- Compatible with ld-decode and decode-orc

**Y/C TBC:**
- Luma (Y) and Chroma (C) components stored separately
- Two files: `.tbcy` (luma) and `.tbcc` (chroma)
- Equivalent to S-Video signal format
- Better quality than composite due to separate channels

### SQLite Metadata Database

Accompanies each TBC file with comprehensive metadata:
- Frame/field numbering and timecode information
- VITC and VBI data extracted from video
- Detected problems (CRC errors, data anomalies)
- Audio synchronization markers
- Signal quality metrics

## Configuration via YAML

All encoding parameters are defined in YAML project files, making configurations:
- **Reproducible** - Version control friendly, human readable
- **Flexible** - Enable/disable features as needed
- **Composable** - Combine multiple sections or effects easily

Key configuration areas:
- **Video system** - PAL or NTSC
- **Input sources** - File paths and frame ranges
- **Output format** - Composite or Y/C, 8-bit or 16-bit
- **Field effects** - Noise, dropouts, timing variations
- **Metadata generators** - VITC, VITS, custom VBI data
- **Processing options** - Filter settings, color ranges
