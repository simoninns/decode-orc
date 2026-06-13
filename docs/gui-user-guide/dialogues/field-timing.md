# Frame Timing

The Frame-timing dialogue is a **diagnostic tool** used to inspect **temporal structure and continuity at the frame level** within a decode-orc pipeline. Like Preview and Frame-scope, it is not a pipeline stage, but a **read-only analysis view** that attaches to preview-capable stages.

The Frame-timing dialogue focuses on **when frames occur and how they relate to one another**, rather than how they look. It is primarily concerned with cadence, continuity, colour-frame sequence, and metadata hints that describe timing.

> **Renamed in v2.0:** This dialogue was called the Field-timing dialogue in Decode-Orc 1.x.

---

## Purpose and use cases

The Frame-timing dialogue is used to:

* Verify colour-frame index continuity (PAL 4-frame cycle, NTSC A/B 2-frame cycle)
* Inspect dropped, duplicated, or skipped frames
* Validate alignment between multiple sources
* Diagnose cadence issues (e.g. irregular frame progression)
* Confirm correct behaviour of frame re-mapping and alignment stages

It is especially useful when working with:

* Multiple captures of the same material
* Pipelines using `frame_map` and/or `source_align`
* Stack-based workflows
* Sources with known timing instability or capture errors

---

## Attaching the Frame-timing dialogue

The Frame-timing dialogue attaches to the **currently previewed stage** and analyses the **post-stage output stream**.

All timing information shown reflects:

* Upstream transforms
* Frame reordering
* Alignment offsets
* Phase correction

---

## Core concepts

### Frame ID

Each output frame is identified by a **FrameID**, corresponding to the 0-based frame identifier used throughout the pipeline.

If frames are reordered or dropped upstream, the FrameIDs shown here reflect the **effective output ordering**, not the original capture numbering.

---

### Colour-frame index

Each frame carries a colour-frame index from the `FrameDescriptor`:

* **PAL / PAL-M:** position within the 4-frame colour sequence (1, 2, 3, or 4 per EBU Tech. 3280-E §1.1.1 / ITU-R BT.1700-1 Annex 1 Part B).
* **NTSC:** A (0) or B (1) per SMPTE 244M-2003 §3.2.
* **Unknown / undetectable:** −1 (shown as "–" in the display).

The dialogue visualises the colour-frame index progression across time, making it easy to spot:

* Phase breaks introduced by capture errors or player skips
* Incorrect sequence after re-mapping
* Effects of the `frame_phase_corrector` stage

---

### Frame continuity

The dialogue tracks continuity of frames over time, highlighting:

* Missing frames
* Duplicated frames (before `frame_map` duplicate removal)
* Unexpected jumps in source frame references
* Padding frames (`is_padding_frame = true`) inserted by `frame_map`

This is critical for diagnosing issues introduced by capture errors or incorrect range mapping.

---

### Field line counts

For each frame the dialogue shows the line count of each field block (field 1 and field 2), making non-standard frame geometry visible at a glance.

---

## Interaction with transform stages

The Frame-timing dialogue reflects the effects of timing-related transform stages directly.

### Frame Map

After `frame_map`, the dialogue shows:

* Reordered frame IDs
* Gaps where ranges were omitted
* Padding frames inserted to fill detected sequence gaps
* Frames removed by duplicate detection

This allows immediate verification that range specifications behave as intended.

---

### Frame Phase Corrector

After `frame_phase_corrector`, the dialogue shows:

* Corrected colour-frame indices (after field swap correction)
* Frames marked with `colour_frame_index = −1` at sequence break points
* Observations for each corrected swap or detected break

This is the primary tool for confirming that phase correction behaved as expected.

---

### Source Align

After `source_align`, the dialogue shows:

* Aligned starting points across sources
* Dropped leading frames
* Colour-frame index consistency across aligned outputs

This is the primary tool for confirming that multiple inputs are correctly synchronised before stacking.

---

### Stacker

After `stacker`, the dialogue shows:

* A single, unified frame timeline
* Stable colour-frame index progression (assuming aligned inputs)
* Absence of per-source discontinuities

It is useful for confirming that stacking did not introduce timing artefacts.

---

## Typical workflows

Common uses of the Frame-timing dialogue include:

* Verifying colour-frame sequence immediately after a source stage
* Confirming correct behaviour of `frame_map` range specifications
* Validating multi-source alignment before stacking
* Diagnosing phase-sequence issues observed in preview output
* Comparing timing behaviour before and after transform changes

---

## Limitations

* The Frame-timing dialogue is read-only and non-destructive.
* It does not display sample-level or image-level data.
* Interpretation assumes that upstream metadata (VBI, timecode, colour burst) is present and valid.

---

## Notes on Frame-timing analysis

* The dialogue always reflects the **current preview stage output**.
* Timing anomalies shown here often explain visual artefacts seen in Preview.
* For best results, use the Frame-timing dialogue together with Preview and Frame-scope.

The Frame-timing dialogue provides essential visibility into the temporal correctness of decode-orc pipelines, complementing visual and waveform-based inspection tools.
