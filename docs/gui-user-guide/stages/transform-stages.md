# Transform stages

Transform stages sit **between source stages and sink stages** in a decode-orc pipeline. They consume one or more upstream stage outputs and produce one or more outputs for downstream stages.

Transform stages are used to:

* Reorder or align captured frames from one or more sources.
* Combine multiple captures into a single improved output.
* Override or edit per-frame metadata and hints used by later processing.
* Apply signal modifications such as dropout correction or masking.

> Terminology note (decode-orc DAGs): stages connect via **connections** between stage inputs and outputs.

---

## Frame Map

| | |
|-|-|
| **Stage id** | `frame_map` |
| **Stage name** | Frame Map |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Reorder frames by specifying explicit ranges, remove duplicates, and pad gaps |

**Use this stage when:**

* You need to reorder or stitch together ranges of frames from a single capture.
* You want to skip a bad region by omitting it from the range list.
* You need to remove duplicate frames detected by matching colour-frame index values.
* You need to pad sequence gaps introduced by player skips.

**What it does**

Frame Map parses a comma-separated list of frame ranges (e.g. `0-10,20-30,11-19`) and remaps output frame IDs to the specified input frames. It can optionally remove consecutive duplicate frames and insert synthetic padding frames to fill detected gaps.

**Parameters**

* `ranges` (string)
    - Comma-separated list of 0-based frame ID ranges.
    - Default: `""` (empty) meaning passthrough.

* `remove_duplicates` (bool)
    - When enabled, removes the second of any two consecutive frames with matching `colour_frame_index` values.
    - Default: `false`.

* `pad_gaps` (bool)
    - When enabled, inserts synthetic padding frames when a break in the `colour_frame_index` sequence is detected (indicating a player skip).
    - Default: `false`.

* `pad_strategy` (string)
    - Strategy for synthesising padding frame content.
    - Allowed values: `nearest`, `black`.
    - Default: `nearest`.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).

---

## Frame Phase Corrector

| | |
|-|-|
| **Stage id** | `frame_phase_corrector` |
| **Stage name** | Frame Phase Corrector |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Detect and correct field-swap artefacts; verify colour-frame sequence continuity |

**Use this stage when:**

* Your capture may contain frames where the two field blocks are in the wrong temporal order (field swap).
* You want to verify that the colour-frame sequence (PAL 4-frame or NTSC 2-frame) is unbroken and mark breaks for downstream handling.

**What it does**

This stage measures the colour burst phase of both field blocks within each frame. When the phases indicate that field 2 is temporally earlier than field 1, the stage presents the frame with the field blocks exchanged (index redirect — no sample data is copied). It also walks the colour-frame index sequence and marks any break with `colour_frame_index = -1`.

**Parameters**

* `correct_field_swap` (bool)
    - When enabled, swaps field blocks whenever burst-phase evidence indicates the fields are in the wrong order.
    - Default: `true`.

* `verify_phase_sequence` (bool)
    - When enabled, walks the `colour_frame_index` sequence and marks breaks with `colour_frame_index = -1`. Emits an observation for each break detected.
    - Default: `true`.

**Observations emitted**

* `frame_phase_corrector.field_swaps_corrected` — number of frames where a field swap was corrected.
* `frame_phase_corrector.phase_breaks_detected` — number of sequence breaks detected.
* `frame_phase_corrector.phase_breaks_marked` — number of frames marked with `colour_frame_index = -1`.

**Recommended pipeline position**

Place `frame_phase_corrector` immediately after the source stage, before `source_align` or `stacker`, so that alignment and stacking operate on correctly sequenced frames.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).

---

## Source Align

| | |
|-|-|
| **Stage id** | `source_align` |
| **Stage name** | Source Align |
| **Connections** | 1–16 inputs → N outputs (one per input) |
| **Purpose** | Align multiple sources so the same output frame index refers to the same underlying disc/tape position in each source |

**Use this stage when:**

* You have multiple captures of the same material that start at different positions.
* You are preparing multiple sources for stacking (typically before `stacker`).
* You used `frame_map` (or otherwise produced streams where output frame 0 does not mean the same real-world frame across sources).

**What it does**

Source Align finds the **first common frame** across all inputs using **VBI frame numbers (CAV)** or **CLV timecodes**, then drops frames as needed so output frame indices are synchronised across all aligned outputs. The stage verifies that `colour_frame_index` is consistent across all aligned sources.

**Parameters**

* `alignmentMap` (string)
    - Manual alignment specification.
    - Format: `input_id+offset` per input, e.g. `1+2, 2+2, 3+1, 4+1`.
    - Default: `""` (empty) meaning auto-detect from VBI/timecode.

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
| **Purpose** | Combine multiple captures into one improved output by stacking corresponding frames |

**Use this stage when:**

* You have multiple captures of the same LaserDisc (or other source) and want to reduce dropouts / noise by combining them.
* You want an in-pipeline equivalent to legacy "disc stacker" workflows.

**What it does**

Stacker expects its inputs to already be aligned (typically using `frame_map` and/or `source_align`). It then stacks "frame N from all sources" into a single output using one of several algorithms. Frames with matching `colour_frame_index` values are aligned for stacking; frames with `colour_frame_index == -1` fall back to temporal alignment. Padding frames (`is_padding_frame = true`) produce no sample accumulation.

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
        - `Disabled` (use audio from the best frame, as determined by video quality)
        - `Mean`
        - `Median`
    - Default: `Mean`.

* `efm_stacking` (string)
    - How to combine EFM t-values across sources.
    - Allowed values:
        - `Disabled` (use EFM from the best frame, as determined by video quality)
        - `Mean`
        - `Median`
    - Default: `Mean`.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).
* Generates a stage report ("Stacker Configuration") that records the effective mode and related configuration.

---

## Dropout Map

| | |
|-|-|
| **Stage id** | `dropout_map` |
| **Stage name** | Dropout Map |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Manually override dropout hints on a per-frame basis without modifying samples |

**Use this stage when:**

* You want to add dropouts that were not detected.
* You want to remove false-positive dropout detections.
* You want to adjust dropout boundaries before correction.
* You want to create custom dropout patterns for testing.

**What it does**

This stage modifies the **dropout hint regions** seen by downstream stages. It does not change the underlying video samples. Dropout specifications are authored in field-line-sample form for compatibility with existing project files; the stage converts these internally to frame-flat sample offsets.

**Parameters**

* `dropout_map` (string)
    - Per-frame dropout overrides in a JSON-like format.
    - Default: `[]`.
    - Example:
        - `[{frame:0,add:[{field:1,line:10,start:100,end:200}],remove:[{field:1,line:15,start:50,end:75}]}]`

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).

---

## Dropout Correction

| | |
|-|-|
| **Stage id** | `dropout_correct` |
| **Stage name** | Dropout Correction |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Correct dropouts by replacing corrupted samples using data from other lines and/or frames |

**Use this stage when:**

* Your source contains dropouts and you want to reconstruct damaged regions.
* You want configurable correction behaviour (intra-field only, search distance limits, etc.).
* You want to visualise what would be corrected.

**What it does**

Dropout Correction reads dropout hints (from the source or from an upstream `dropout_map` stage), then replaces affected samples using nearby lines and/or the opposite field block within the frame, subject to configured constraints. Correction operates on the full frame buffer regardless of any active-area crop applied upstream.

**Parameters**

* `overcorrect_extension` (uint32)
    - Extend dropout regions by this many samples.
    - Range: 0–48.
    - Default: 0.

* `intrafield_only` (bool)
    - When enabled, forces correction using only the same field block (never the opposite block).
    - Default: `false`.

* `max_replacement_distance` (uint32)
    - Maximum distance (in lines) to search for replacement data.
    - Range: 1–50.
    - Default: 10.

* `match_chroma_phase` (bool)
    - When enabled, matches chroma phase when selecting replacement lines (PAL only).
    - Default: `true`.

* `highlight_corrections` (bool)
    - When enabled, fills corrected regions with white level (100 IRE) to visualise dropout locations.
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
| **Purpose** | Mask (blank) specified lines by 0-based frame-flat line index |

**Use this stage when:**

* You want to hide visible VBI content.
* You want to mask the NTSC closed-caption line.
* You want to blank other unwanted content in fixed line regions.

**What it does**

Mask Line overwrites selected frame lines with a constant level defined in IRE units. Selection is driven by a line specification string that supports field-parity qualifiers. Line numbers are **0-based frame-flat** indices. For PAL, any 1136-sample line (at the four non-orthogonal positions) is zeroed across all 1136 samples.

**Parameters**

* `lineSpec` (string)
    - Lines to mask.
    - Format: `PARITY:LINE` or `PARITY:START-END`.
    - Parity:
        - `F` = field 1 lines only
        - `S` = field 2 lines only
        - `A` = all lines
    - Examples:
        - `F:21` — mask line 21 of field 1
        - `S:6-22` — mask lines 6–22 of field 2
        - `A:10,F:21`
    - Line numbers are 0-based frame-flat indices.
    - Default: `""` (no masking).

* `maskIRE` (double)
    - IRE level to write (0 = black, 100 = white).
    - Range: 0.0–100.0.
    - Default: 0.0.

**Analysis / preview tools**

* Supports standard GUI previews (via `PreviewableStage`).

---

## Frame Field Swap

| | |
|-|-|
| **Stage id** | `frame_field_swap` |
| **Stage name** | Frame Field Swap |
| **Connections** | 1 input → 1 output (fan-out supported) |
| **Purpose** | Swap the two field blocks within each frame (intra-frame field reorder) |

**Use this stage when:**

* Field order detection is incorrect and you need to flip it within the frame.
* You want to test the effect of swapped field order on later processing or rendering.

**What it does**

This stage does not copy or modify sample data. It presents the output frame with field line blocks in reverse order (field 2 block first, field 1 block second) via an index redirect inside the frame representation wrapper.

> **Renamed in v2.0:** This stage was called `field_invert` in Decode-Orc 1.x. Existing project files that reference `field_invert` must be recreated using Decode-Orc 2.0.

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
| **Purpose** | Override video parameter hints (dimensions, signal levels, active-line boundaries) |

**Use this stage when:**

* Source metadata is missing or incorrect.
* You need to adjust active video boundaries for later processing.
* You need to override signal levels for correct black/white interpretation.
* You need to force dimensions to match a known-good configuration.

**What it does**

Video Parameters wraps the upstream frame representation and overrides specified `SourceParameters` fields. Any parameter left at `-1` is inherited from the source. When any level override (`black_level`, `white_level`) is applied, the stage sets `has_nonstandard_values = true`. When any crop parameter is set, the stage sets `active_area_cropping_applied = true`.

All level values are in the CVBS_U10_4FSC 10-bit domain.

**Parameters**

All parameters are int32 and use `-1` as "inherit from source".

* `frame_width_nominal` — Override nominal frame width in samples.
* `frame_height` — Override frame height in lines.
* `sync_tip_level` — Override sync tip level (10-bit domain).
* `blanking_level` — Override blanking level (10-bit domain).
* `black_level` — Override black level (10-bit domain). Sets `has_nonstandard_values = true`.
* `white_level` — Override white level (10-bit domain). Sets `has_nonstandard_values = true`.
* `peak_level` — Override peak level (10-bit domain).
* `active_video_start` — Override active video start sample index. Sets `active_area_cropping_applied = true`.
* `active_video_end` — Override active video end sample index. Sets `active_area_cropping_applied = true`.
* `first_active_frame_line` — Override first active frame line. Sets `active_area_cropping_applied = true`.
* `last_active_frame_line` — Override last active frame line. Sets `active_area_cropping_applied = true`.
