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

**Stage tools**

* **Disc Mapper** — analyses the incoming frame stream for tracking errors (skipped, repeated, or missing frames caused by LaserDisc player seek problems) and writes a corrected `ranges` specification back to the stage. Works for both CAV and CLV discs; the analysis can be cancelled from the tool dialog. LaserDisc sources only.
* **Frame Map Range Finder** — locates a section of the disc by picture number or CLV timecode (e.g. `12345` or `0:1:23.4`) and writes the corresponding `ranges` string to the stage. Uses a binary-chop search over the disc's VBI picture numbers, falling back to a sequential scan when the numbering is not monotonic. Cancellable. LaserDisc sources only.
* **Frame Corruption Generator** — injects synthetic corruption patterns (skips, repeats, gaps) into the frame mapping for testing the Disc Mapper tool.
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

Source Align finds the **first common frame** across all inputs using **VBI frame numbers (CAV)** or **CLV timecodes**, then drops the leading fields that precede it in each input so output frame indices are synchronised across all aligned outputs. The stage verifies that `colour_frame_index` is consistent across all aligned sources.

**Parameters**

* `alignmentMap` (string)
    - Manual alignment specification. When set, automatic detection is skipped and the specified per-input offsets (in fields) are applied directly.
    - Format: `input_id+offset` per input, e.g. `1+2, 2+2, 3+1, 4+1`.
    - Default: `""` (empty) meaning auto-detect from VBI/timecode.

**Stage tools**

* **Source Alignment Analysis** — analyses all connected sources and writes the optimal offset map to `alignmentMap`. Two alignment modes are offered: `pad_for_alignment` prepends synthetic padding frames so all sources start from the earliest VBI frame available (no frames are lost), while `first_common_frame` trims all sources to the first VBI frame present in every input (may discard unique leading content). Cancellable. LaserDisc sources only.
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
    - Stacking algorithm, applied per pixel to the usable (non-dropout) values across all sources.
    - Default: `Auto`.

| Mode | Behaviour per pixel |
|------|--------------------|
| `Auto` | `Smart Mean` when 3 or more usable source values are available; plain `Mean` otherwise. |
| `Mean` | Arithmetic mean of all usable source values. |
| `Median` | Median of the usable source values (mean of the middle two for an even count). |
| `Smart Mean` | Mean of the values within `smart_threshold` of the median, rejecting outliers; falls back to the median when no value qualifies. |
| `Smart Neighbor` | Not yet implemented — currently behaves as `Median`. |
| `Neighbor` | Not yet implemented — currently behaves as `Median`. |

* `smart_threshold` (int32)
    - Pixel-value threshold used by `Smart Mean` (and by `Auto` when it selects `Smart Mean`) to exclude outliers before averaging.
    - Range: 0–128.
    - Default: 15.
    - Has no effect for other modes.

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
        - `Disabled` (use audio from the source with the fewest dropouts)
        - `Mean`
        - `Median`
    - Default: `Mean`.

* `efm_stacking` (string)
    - How to combine EFM t-values across sources.
    - Allowed values:
        - `Disabled` (use EFM from the source with the fewest dropouts)
        - `Mean`
        - `Median`
    - Default: `Mean`.

**Notes**

* Stacking reduces noise only when the sources contain independent noise (separate captures). Sources that are identical apart from dropouts stack to the same underlying signal, so SNR will not improve on dropout-free picture areas.

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
| **Purpose** | Manually override dropout hints on a per-field basis without modifying samples |

**Use this stage when:**

* You want to add dropouts that were not detected.
* You want to remove false-positive dropout detections.
* You want to adjust dropout boundaries before correction.
* You want to create custom dropout patterns for testing.

**What it does**

This stage modifies the **dropout hint regions** seen by downstream stages. It does not change the underlying video samples. For each field with an entry in the `dropout_map` parameter, additions are merged into the hint list and removals are subtracted before the hints are passed downstream; fields without an entry are forwarded unchanged.

**Parameters**

* `dropout_map` (string)
    - Per-field dropout overrides in a JSON-like format.
    - Default: `[]`.
    - Example:
        - `[{field:0,add:[{field:1,line:10,start:100,end:200}],remove:[{field:1,line:15,start:50,end:75}]}]`
    - Use the Dropout Editor tool to build this string interactively rather than writing it by hand.

**Stage tools**

* **Dropout Editor** — opens an interactive editor for drawing new dropout regions or deleting existing ones on a per-field basis. Changes are saved back to the `dropout_map` parameter automatically.
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

Mask Line overwrites selected frame lines with a constant 10-bit sample level. Line numbers are **0-based frame-flat** indices. For Y/C sources both the luma and chroma buffers of a masked line are filled with the mask value. All other lines, and the audio/EFM streams, are forwarded unchanged.

**Parameters**

* `lineSpec` (string)
    - Comma-separated list of frame-flat 0-based line indices or inclusive ranges to mask.
    - Format: `LINE` or `START-END`.
    - Examples:
        - `20` — mask frame-flat line 20 (broadcast line 21, field 1 in PAL/NTSC)
        - `5-21` — mask frame-flat lines 5 through 21 (broadcast lines 6–22)
        - `5-21,318-334` — mask the VBI area in both fields of a PAL frame
    - Default: `""` (no masking, pass-through).
    - Use the **Mask Line Config** tool to generate this value from broadcast line numbers without writing the spec manually.

* `maskSampleLevel` (integer)
    - 10-bit sample level (0–1023) written to all masked lines.
    - Default: 0 (sync tip).
    - Typical values: 240 = blanking/black (NTSC/PAL-M), 256 = blanking/black (PAL), 800 = white (NTSC/PAL-M), 844 = white (PAL).

**Stage tools**

* **Mask Line Config** — a configuration dialog that accepts **full-frame broadcast line numbers** (1-based; PAL 1–625, NTSC/PAL-M 1–525) and converts them automatically to the frame-flat `lineSpec` format, masking each range in **both** fields. Example: PAL range 6–22 produces `lineSpec` = `5-21,318-334`.
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
