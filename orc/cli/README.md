# orc-process - TBC Processing CLI Tool

## Overview

`orc-process` is a command-line tool that executes observer pipelines on TBC (Time Base Corrected) files. It processes video fields through configured observers and generates complete SQLite metadata databases.

## Usage

```bash
orc-process --dag <pipeline.yaml> <input.tbc> <output.tbc>
```

**Arguments:**
- `--dag <pipeline.yaml>` - YAML file defining the observer pipeline
- `<input.tbc>` - Input TBC file (with accompanying `.tbc.db` metadata)
- `<output.tbc>` - Output TBC file (video data copied, new metadata generated)

## Features

- **YAML Pipeline Configuration**: Define observer chains in declarative YAML
- **Complete Metadata Regeneration**: Creates fresh SQLite metadata from scratch
- **7 Observer Types**: Biphase, VITC, ClosedCaption, VideoId, FmCode, WhiteFlag, VITS
- **Progress Tracking**: Real-time field processing progress
- **Statistics Output**: Summary of observations with averages
- **ld-analyse Compatible**: Output metadata works with legacy tools

## Example Pipeline

**examples/vbi-observers.yaml:**
```yaml
name: "VBI Observer Pipeline"
version: "1.0"

observers:
  - type: biphase
    enabled: true
    
  - type: vitc
    enabled: true
    
  - type: closed_caption
    enabled: true
    
  - type: video_id
    enabled: true
    
  - type: fm_code
    enabled: true
    
  - type: white_flag
    enabled: true
    
  - type: vits
    enabled: true
```

## Processing Example

```bash
orc-process --dag examples/vbi-observers.yaml \
  test-data/laserdisc/ntsc/cinder/9000-9210/cinder_ntsc_clv_9000-9210.tbc \
  output/cinder_processed.tbc
```

**Output:**
```
Loading DAG: examples/vbi-observers.yaml
  Pipeline: VBI Observer Pipeline v1.0
  Observers configured: 7
  Enabling observer: biphase
  Enabling observer: vitc
  Enabling observer: closed_caption
  Enabling observer: video_id
  Enabling observer: fm_code
  Enabling observer: white_flag
  Enabling observer: vits

Copying video data...
Loading TBC representation...

Executing pipeline...
Processing 420 fields...
  Progress: 420/420 (100%)

Observer Results:
  Biphase (VBI):        420 fields
  VITC Timecode:        0 fields
  Closed Captions:      210 fields
  VITS Metrics:         420 fields (avg white SNR: 38.1134 dB, avg black PSNR: 41.5262 dB)
  Video ID:             0 fields
  FM Code:              0 fields
  White Flag:           420 fields

Done! Output written to:
  output/cinder_processed.tbc
  output/cinder_processed.tbc.db
```

## Output Database Schema

The generated SQLite database includes:

### Core Tables
- **capture** - Video parameters (format, sample rate, dimensions, decoder='orc-process')
- **field_record** - Per-field metadata (field_id, parity, line count, NTSC-specific fields)
- **pcm_audio_parameters** - Audio settings (if present in input)

### Observer Result Tables
- **vbi** - Bi-phase encoded VBI data (picture numbers, chapter markers, CLV timecodes)
- **vitc** - VITC timecode observations
- **closed_caption** - Closed caption data (NTSC)
- **vits_metrics** - VITS quality measurements (white SNR, black PSNR)

All observer tables include `capture_id` and `field_id` columns, with field_id using 0-based indexing (0 to N-1).

## Observer Details

### Biphase Observer
Decodes bi-phase (Manchester) encoded data from PAL lines 16-18 and NTSC line 16.
- Picture numbers (CAV frame numbers)
- Chapter markers
- CLV timecodes

### VITC Observer
Extracts VITC timecode from video lines.

### Closed Caption Observer
Decodes NTSC closed caption data (line 21).

### Video ID Observer
Extracts NTSC video ID information.

### FM Code Observer
Decodes NTSC FM code data.

### White Flag Observer
Detects NTSC white flag indicators.

### VITS Quality Observer
Analyzes Vertical Interval Test Signals for quality metrics:
- **White SNR** (Signal-to-Noise Ratio) in dB
- **Black PSNR** (Peak Signal-to-Noise Ratio) in dB

**Precision**: Observers calculate at full precision, values rounded to 4 decimal places when written to database.

## Configuration

Observers can be individually enabled/disabled in the pipeline YAML:

```yaml
observers:
  - type: vits
    enabled: true  # Set to false to disable
```

## Batch Processing

Use the provided script to process multiple files:

```bash
bash process-test-data.sh
```

This processes all test files in `test-data/` using the VBI observers pipeline and creates mirrored output in `test-output/`.

## Technical Details

- **Field ID Indexing**: 0-based sequential (field 0 to N-1)
- **Video Data**: Copied directly from input to output TBC
- **Metadata**: Regenerated from scratch, not copied from input
- **Memory**: Streams field data, does not load entire TBC into RAM
- **Compatibility**: Output viewable with `ld-analyse`

## See Also

- [Observer Framework Documentation](../core/include/observer.h)
- [Pipeline Format](../../docs/DAG.md)
- [Test Data Inventory](../../test-data/TEST_DATA_INVENTORY.md)
