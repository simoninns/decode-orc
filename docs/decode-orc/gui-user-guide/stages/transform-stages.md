# Transform stages

Transform stages sit **between source stages and sink stages** in a decode-orc pipeline. They consume one or more upstream stage outputs and produce one or more outputs for downstream stages.

Transform stages are used to:

* Reorder or align captured fields from one or more sources.
* Combine multiple captures into a single improved output.
* Override or edit per-field metadata and hints used by later processing.
* Apply signal modifications such as dropout correction or masking.

> Terminology note (decode-orc DAGs): stages connect via **connections** between stage inputs and outputs.

---

## Field Map

| | |
|-|-|
| **Stage id** | `field_map` |
| **Stage name** | Field Map |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Reorder fields by specifying explicit ranges |

**Use this stage when:**

* You need to reorder or stitch together ranges of fields from a single capture.
* You want to skip a bad region by omitting it from the range list.
* You want to produce a “virtual” reordered view without copying the underlying field data.

**What it does**

Field Map parses a comma-separated list of field ranges (e.g. `0-10,20-30,11-19`) and remaps output field indices to the specified input fields.

**Parameters**

* `ranges` (string)
    - Comma-separated list of field ranges.
    - Default: `""` (empty) meaning passthrough.

* `seed` (int32)
    - "Random seed used to generate field corruption pattern (for reproducibility)".
    - Default: `0`.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).

---

## Source Align

| | |
|-|-|
| **Stage id** | `source_align` |
| **Stage name** | Source Align |
| **Connections** | 1–16 inputs → N outputs (one per input) |
| **Purpose** | Align multiple sources so the same output field index refers to the same underlying disc/tape position in each source |

**Use this stage when:**

* You have multiple captures of the same material that start at different positions.
* You are preparing multiple sources for stacking (typically before `stacker`).
* You used `field_map` (or otherwise produced streams where output field 0 does not mean the same real-world field across sources).

**What it does**

Source Align finds the **first common field** across all inputs using **VBI frame numbers (CAV)** or **CLV timecodes**, then drops fields as needed so output field indices are synchronised across all aligned outputs.

**Parameters**

* `alignmentMap` (string)
    - Manual alignment specification.
    - Format: `input_id+offset` per input, e.g. `1+2, 2+2, 3+1, 4+1`.
    - Default: `""` (empty) meaning auto-detect from VBI/timecode.

* `enforceFieldOrder` (bool)
    - When enabled, ensures the first output field is always a "first field" (adds one extra field if needed).
    - Default: `true`.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).
* Generates a stage report after execution, including per-input alignment offsets and related alignment details.

---

## Stacker

| | |
|-|-|
| **Stage id** | `stacker` |
| **Stage name** | Stacker |
| **Connections** | 1–16 inputs → 1 output (fan-out supported) |
| **Purpose** | Combine multiple captures into one improved output by stacking corresponding fields |

**Use this stage when:**

* You have multiple captures of the same LaserDisc (or other source) and want to reduce dropouts / noise by combining them.
* You want an in-pipeline equivalent to legacy “disc stacker” workflows.

**What it does**

Stacker expects its inputs to already be aligned (typically using `field_map` and/or `source_align`). It then stacks “field N from all sources” into a single output using one of several algorithms.

If only **1 input** is provided, the stage acts as a passthrough.

**Parameters**

* `mode` (string)
    - Stacking algorithm.
    - Allowed values:
        - `Auto`
        - `Mean`
        - `Median`
        - `Smart Mean`
        - `Smart Neighbor`
        - `Neighbor`
    - Default: `Auto`.

* `smart_threshold` (int32)
    - Threshold used by smart modes.
    - Range: 0–128.
    - Default: 15.
    - Only used when `mode` is `Smart Mean` or `Smart Neighbor`.

* `no_diff_dod` (bool)
    - Disable differential dropout detection.
    - Default: `false`.
    - When `false`, the stage may recover pixels marked as dropouts by comparing values across sources (requires 3+ sources).

* `passthrough` (bool)
    - Passthrough "universal" dropouts that appear in all sources.
    - Default: `false`.

* `audio_stacking` (string)
    - How to combine audio across sources.
    - Allowed values:
        - `Disabled` (use audio from the best field, as determined by video quality)
        - `Mean`
        - `Median`
    - Default: `Mean`.

* `efm_stacking` (string)
    - How to combine EFM t-values across sources.
    - Allowed values:
        - `Disabled` (use EFM from the best field, as determined by video quality)
        - `Mean`
        - `Median`
    - Default: `Mean`.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).
* Generates a stage report (“Stacker Configuration”) that records the effective mode and related configuration.

---

## Dropout Map

| | |
|-|-|
| **Stage id** | `dropout_map` |
| **Stage name** | Dropout Map |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Manually override dropout hints on a per-field basis without modifying samples |

**Use this stage when:**

* You want to add dropouts that were not detected.
* You want to remove false-positive dropout detections.
* You want to adjust dropout boundaries before correction.
* You want to create custom dropout patterns for testing.

**What it does**

This stage modifies the **dropout hint regions** seen by downstream stages. It does not change the underlying video samples.

**Parameters**

* `dropout_map` (string)
    - Per-field dropout overrides in a JSON-like format.
    - Default: `[]`.
    - Example:
        - `[{field:0,add:[{line:10,start:100,end:200}],remove:[{line:15,start:50,end:75}]}]`

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).

---

## Dropout Correction

| | |
|-|-|
| **Stage id** | `dropout_correct` |
| **Stage name** | Dropout Correction |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Correct dropouts by replacing corrupted samples using data from other lines and/or fields |

**Use this stage when:**

* Your source contains dropouts and you want to reconstruct damaged regions.
* You want configurable correction behaviour (intra-field only, search distance limits, etc.).
* You want to visualise what would be corrected.

**What it does**

Dropout Correction reads dropout hints (from the source or from an upstream `dropout_map` stage), then replaces affected samples using nearby lines and/or the opposite field, subject to configured constraints.

**Parameters**

* `overcorrect_extension` (uint32)
    - Extend dropout regions by this many samples.
    - Range: 0–48.
    - Default: 0.

* `intrafield_only` (bool)
    - When enabled, forces correction using only the same field (never the opposite field).
    - Default: `false`.

* `max_replacement_distance` (uint32)
    - Maximum distance (in lines) to search for replacement data.
    - Range: 1–50.
    - Default: 10.

* `match_chroma_phase` (bool)
    - When enabled, matches chroma phase when selecting replacement lines (PAL only).
    - Default: `true`.

* `highlight_corrections` (bool)
    - When enabled, fills corrected regions with white IRE level (100) to visualise dropout locations.
    - Default: `false`.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).
* The `highlight_corrections` parameter is specifically intended as an analysis/diagnostic view.

---

## Mask Line

| | |
|-|-|
| **Stage id** | `mask_line` |
| **Stage name** | Mask Line |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Mask (blank) specified lines in specified fields by parity |

**Use this stage when:**

* You want to hide visible VBI content.
* You want to mask the NTSC closed-caption line (traditional “line 21”, represented here as field line index 20).
* You want to blank other unwanted content in fixed line regions.

**What it does**

Mask Line overwrites selected field lines with a constant level defined in IRE units. Selection is driven by a line specification string that supports parity qualifiers.

**Parameters**

* `lineSpec` (string)
    - Lines to mask.
    - Format: `PARITY:LINE` or `PARITY:START-END`.
    - Parity:
        - `F` = first field only
        - `S` = second field only
        - `A` = all fields
    - Examples:
        - `F:21`
        - `S:6-22`
        - `A:10,F:21`
    - Line numbers are 0-based field line numbers.
    - Default: `""` (no masking).

* `maskIRE` (double)
    - IRE level to write (0 = black, 100 = white).
    - Range: 0.0–100.0.
    - Default: 0.0.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).

---

## Field Invert

| | |
|-|-|
| **Stage id** | `field_invert` |
| **Stage name** | Field Invert |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Invert field parity hints (swap first/second field hints) |

**Use this stage when:**

* Field order detection is incorrect and you need to flip it.
* You want to test the effect of swapped field order on later processing or rendering.

**What it does**

This stage does not modify sample data. It flips the `is_first_field` parity hint for each field.

**Parameters**

* None.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).

---

## Video Parameters

| | |
|-|-|
| **Stage id** | `video_params` |
| **Stage name** | Video Parameters |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Override video parameter hints (dimensions, sample ranges, IRE levels, active-line hints) |

**Use this stage when:**

* Source metadata is missing or incorrect.
* You need to adjust active video boundaries for later processing.
* You need to override IRE levels for correct black/white interpretation.
* You need to force dimensions or burst positions to match a known-good configuration.

**What it does**

Video Parameters builds an override `SourceParameters` set from its configured values. Any parameter left at `-1` is inherited from the input source. It also derives an **active line hint** from the overridden parameters when possible.

**Parameters**

All parameters are int32 and use `-1` as “inherit from source”.

* `fieldWidth`
    - Override field width in samples.
    - Default: `-1`.

* `fieldHeight`
    - Override field height in lines.
    - Default: `-1`.

* `colourBurstStart`
    - Override burst start sample index.
    - Default: `-1`.

* `colourBurstEnd`
    - Override burst end sample index.
    - Default: `-1`.

* `activeVideoStart`
    - Override active video start sample index.
    - Default: `-1`.

* `activeVideoEnd`
    - Override active video end sample index.
    - Default: `-1`.

* `firstActiveFieldLine`
    - Override first active field line.
    - Default: `-1`.

* `lastActiveFieldLine`
    - Override last active field line.
    - Default: `-1`.

* `white16bIRE`
    - Override white level in 16-bit IRE units.
    - Default: `-1`.

* `black16bIRE`
    - Override black level in 16-bit IRE units.
    - Default: `-1`.
