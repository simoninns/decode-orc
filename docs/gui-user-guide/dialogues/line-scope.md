# Frame scope

The Frame-scope is an **interactive signal inspection tool** within decode-orc that allows detailed examination of **individual video lines at the sample level**. Like the Preview dialogue, it is not a pipeline stage, but a **UI analysis tool** that attaches to preview-capable stages.

The Frame-scope is primarily intended for **low-level signal analysis**, making it possible to inspect timing, levels, noise, burst structure, and dropout behaviour with precision that is not possible from image-based preview alone.

> **Renamed in v2.0:** This dialogue was called the Line-scope in Decode-Orc 1.x.

---

## Purpose and use cases

The Frame-scope is used to:

* Inspect raw luma and chroma waveforms
* Verify sync tip, blanking, black, white, and peak levels in CVBS_U10_4FSC 10-bit units and in millivolts
* Examine colour burst amplitude and phase
* Diagnose noise, ringing, or capture artefacts
* Validate dropout detection and correction behaviour
* Compare line data before and after transform stages

It is especially valuable when working with:

* Analogue captures
* LaserDisc RF-derived signals
* PAL/NTSC timing and level issues
* Dropout-heavy or marginal sources

---

## Attaching the Frame-scope

The Frame-scope attaches to the **currently previewed stage** and reflects the same frame and timing context as the Preview dialogue.

When active, it operates on:

* The currently selected frame (identified by `FrameID`)
* A single selected frame line (0-based frame-flat index)
* The post-stage output signal (including all upstream transforms)

---

## Core Frame-scope features

### Line selection and numbering modes

The user selects a specific line using one of four numbering modes:

| Mode | Description |
|------|-------------|
| **Frame flat (0-based)** | Raw 0-based frame line index, as stored internally |
| **Frame sequential (1-based)** | 1-based sequential line number within the frame |
| **Field relative** | Line number within the field (1-based), prefixed by field number |
| **Broadcast interlaced** | Standard broadcast line number per ITU-R BT.470-6 / SMPTE 170M-2004 |

The selected mode is shown in a drop-down and remembered per video system. Switching mode does not change the selected line; only the displayed label changes.

Key characteristics:

* Line indices reflect any upstream re-mapping or masking
* PAL lines correctly handle the four 1136-sample non-orthogonal positions
* The correct sample count per line (1135 or 1136 for PAL) is shown in the display header

---

### Sample-level waveform display

The Frame-scope displays signal amplitude **per sample** across the selected line.

The Y axis extends below 0 mV (to sync tip and below) and above 100 IRE (to peak and beyond) — no arbitrary clamping of the display range. When a sample value outside `[sync_tip, peak]` is present, the axis expands to include it.

The waveform typically includes:

* Sync tip
* Back porch
* Colour burst (if present)
* Active video region

Reference level markers are drawn at all five normative levels with their standard name and mV value:

| Level | PAL (mV) | NTSC (mV) |
|-------|----------|-----------|
| Sync tip | −300 | −286 |
| Blanking | 0 | 0 |
| Black | +54 | +54 |
| White | +700 | +714 |
| Peak | +933 | +900 |

Amplitude is shown in millivolts, derived per ITU-R BT.1700-1 / SMPTE 170M-2004 §11.4.

---

### Channel views

Depending on pipeline configuration and stage capabilities, the Frame-scope may support viewing:

* Luma (Y)
* Chroma (composite or decoded)
* Combined signal (where applicable)

The exact available channels depend on the upstream stages and signal type.

---

## Interaction with transform stages

The Frame-scope reflects **exactly what a downstream stage will see**.

Examples:

* After `video_params`, black/white level overrides are visible immediately.
* After `mask_line`, masked regions appear flattened at the mask IRE level.
* After `dropout_correct`, corrected samples can be inspected directly.
* After `stacker`, per-sample noise reduction effects are visible.

This makes the Frame-scope ideal for validating the **numerical effect** of transforms.

---

## Dropout and correction inspection

When dropout hints are present, the Frame-scope can be used to:

* Inspect the original corrupted samples
* Verify the extent of dropout regions
* Confirm that replacement data is reasonable
* Compare corrected vs uncorrected behaviour by toggling upstream stages

When `highlight_corrections` is enabled upstream, corrected regions appear clearly in the waveform.

---

## Timing and stability analysis

The Frame-scope is frequently used to:

* Verify horizontal timing stability
* Inspect sync edge shape and jitter
* Check burst placement and consistency
* Compare timing between aligned sources

These checks are essential when diagnosing capture hardware issues or alignment problems.

---

## Limitations

* The Frame-scope is read-only and non-destructive.
* Only one line can be inspected at a time.
* Performance depends on pipeline complexity and preview position.
* Some sink-only or hardware-output stages do not support Frame-scope attachment.

---

## Typical Frame-scope workflows

Common workflows include:

* Inspecting colour burst before and after chroma-related transforms
* Verifying black/white levels after `video_params`
* Examining dropout regions before applying correction
* Comparing stacked vs unstacked signal noise
* Diagnosing capture artefacts at the sample level

---

## Notes on Frame-scope usage

* The Frame-scope always reflects the **current preview frame and stage**.
* Frame-scope analysis complements, rather than replaces, image-based preview.
* For accurate interpretation, ensure video parameters upstream are correct.

The Frame-scope is a critical tool for decode-orc's low-level, signal-focused workflows, providing visibility into the exact waveform data that underpins all higher-level processing.
