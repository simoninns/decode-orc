# orc-process Pipeline Examples

This directory contains example YAML configuration files for the orc-process tool.

## Basic Usage

```bash
orc-process --dag <pipeline.yaml> <input.tbc> <output.tbc>
```

## Example Pipelines

### VBI Extraction

**vbi-observers.yaml** - Extract all VBI metadata (biphase, VITC, closed captions, etc.)
```bash
orc-process --dag vbi-observers.yaml capture.tbc analyzed.tbc
```
Output: `analyzed.tbc.db` with VBI data tables compatible with ld-analyse

---

### Dropout Correction

**dropout-correct.yaml** - Basic automatic dropout correction with default parameters
```bash
orc-process --dag dropout-correct.yaml input.tbc corrected.tbc
```
Uses dropout hints from the TBC decoder metadata and corrects using intra/interfield line replacement.

**dropout-correct-with-decisions.yaml** - Manual dropout corrections with user decisions
```bash
orc-process --dag dropout-correct-with-decisions.yaml input.tbc corrected.tbc
```
Demonstrates:
- **ADD**: Create new dropout regions missed by detector
- **REMOVE**: Remove false positive detections
- **MODIFY**: Adjust boundaries of detected dropouts

Edit the `dropout_decisions:` section in the YAML to add your own corrections.

**dropout-correct-overcorrect.yaml** - Aggressive correction for heavily damaged sources
```bash
orc-process --dag dropout-correct-overcorrect.yaml damaged.tbc restored.tbc
```
Uses `overcorrect_extension: 24` to extend all dropouts by 24 samples. Best for disc rot or severe tape damage.

**dropout-correct-intrafield.yaml** - Conservative intrafield-only correction
```bash
orc-process --dag dropout-correct-intrafield.yaml input.tbc corrected.tbc
```
Only uses lines from the same field for correction, avoiding field blending artifacts. Best for high-quality progressive content.

**complete-pipeline.yaml** - Full VBI extraction + dropout correction workflow
```bash
orc-process --dag complete-pipeline.yaml capture.tbc restored.tbc
```
Runs all observers and correction in a single pass.

---

## Configuration Reference

### Observers

Available observer types:
- `biphase` - Extract VBI picture numbers, CLV timecodes, chapter markers (lines 16-18)
- `vitc` - Extract VITC timecode (lines 6-22 PAL, 10-20 NTSC)
- `closed_caption` - Decode CEA-608 closed captions (line 21 NTSC, 22 PAL)
- `video_id` - Extract NTSC video ID (IEC 61880) with aspect ratio and CGMS-A (line 20)
- `fm_code` - Decode NTSC FM-coded data (line 10)
- `white_flag` - Detect NTSC white flag (line 11)
- `vits` - Calculate VITS quality metrics (white SNR, black PSNR)

### Stages

Available stage types:

#### `dropout_correct`
Corrects video dropouts by replacing corrupted samples with data from nearby lines.

**Parameters:**
- `overcorrect_extension: 0-24` (default: 0)
  - Extend detected dropouts by N samples
  - Use 24 for "overcorrect mode" on heavily damaged sources
  
- `intrafield_only: true|false` (default: false)
  - If true, only use lines from the same field for correction
  - Avoids field blending, preserves progressive-like quality
  
- `reverse_field_order: true|false` (default: false)
  - Use second field first instead of first field first
  - Rarely needed, for non-standard field ordering
  
- `max_replacement_distance: 1-10` (default: 10)
  - Maximum number of lines to search for replacement
  - Larger values find better matches but are slower
  
- `match_chroma_phase: true|false` (default: true)
  - Match chroma phase when selecting replacement lines
  - Set to false for B&W content

### Dropout Decisions

User decisions modify the dropout observer's detections before correction:

```yaml
dropout_decisions:
  - field_id: 100        # FieldID (0-based)
    line: 50             # Line number in field
    start_sample: 500    # First corrupted sample
    end_sample: 550      # Last corrupted sample
    action: "add"        # "add", "remove", or "modify"
    notes: "Optional description"
```

**Actions:**
- **add** - Create a new dropout region not detected by the observer
- **remove** - Mark a false positive as not a dropout
- **modify** - Adjust the boundaries of a detected dropout (replace entire region)

## Tips

1. **TBC hints are the source** - Dropout correction uses hints from the decoder (ld-decode)
2. **Review in ld-analyse** - Use ld-analyse to visually inspect dropout locations
3. **Add manual corrections** - Edit YAML to add/remove/modify specific dropouts
4. **Iterate** - Rerun with adjusted decisions until quality is acceptable
5. **Use overcorrect for damaged discs** - If you see residual dropout artifacts, try overcorrect mode
6. **Use intrafield for progressive content** - Animation or computer-generated content benefits from intrafield-only

## Creating Custom Pipelines

Copy an existing example and modify:

```yaml
name: "My Custom Pipeline"
version: "1.0"

observers:
  - type: dropout
    enabled: true
  - type: biphase
    enabled: true

dropout_decisions:
  # Add your manual corrections here
  - field_id: 42
    line: 100
    start_sample: 500
    end_sample: 600
    action: "add"
    notes: "Visible corruption"

stages:
  - type: dropout_correct
    enabled: true
    overcorrect_extension: 0
    intrafield_only: false
    reverse_field_order: false
    max_replacement_distance: 10
    match_chroma_phase: true
```

Save as `my-pipeline.yaml` and run:
```bash
orc-process --dag my-pipeline.yaml input.tbc output.tbc
```
