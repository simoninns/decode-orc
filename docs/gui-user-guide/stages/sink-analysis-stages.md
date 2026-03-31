# Sink (Analysis) Stages

Analysis sink stages are **terminal stages** that generate diagnostics, metrics, and reports rather than producing media or hardware output. They consume processed data from upstream stages and emit **analysis results** intended for comparison, validation, or debugging.

Analysis sinks:

* Do not modify video, audio, or metadata
* Do not produce outputs that can be connected further downstream
* May write reports to disk and/or emit structured statistics to the UI

They are typically used to:

* Compare capture quality across multiple sources
* Validate signal stability and decode quality
* Quantify the effects of transform stages such as stacking or dropout correction

---

## Burst Level Analysis Sink

| | |
|-|-|
| **Stage id** | `burst_level_analysis` |
| **Stage name** | Burst Level Analysis Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Measure colour burst level stability across fields |

**Use this stage when:**

* Evaluating chroma signal stability
* Comparing multiple captures of the same source
* Diagnosing colour amplitude fluctuations or capture issues

**What it does**

This stage measures the amplitude of the colour burst for each field and generates statistics describing burst level variation over time.

The analysis is typically performed on the burst window defined by the active video parameters (either from the source or overridden upstream).

**Parameters**

* None.

**Analysis output**

* Per-field burst amplitude measurements
* Aggregate statistics (mean, variance, min/max)
* Optional time-series data for plotting burst stability

**Notes**

* Results are meaningful only if colour burst timing is correct upstream.
* Masking or altering the burst region before this stage will invalidate results.

---

## Dropout Analysis Sink

| | |
|-|-|
| **Stage id** | `dropout_analysis` |
| **Stage name** | Dropout Analysis Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Produce statistics describing dropout frequency, size, and distribution |

**Use this stage when:**

* Comparing dropout levels between captures
* Evaluating the effectiveness of stacking or dropout correction
* Identifying problematic regions of a capture

**What it does**

This stage reads dropout hints present in the stream (originating from the source or modified by transform stages such as `dropout_map`) and generates statistical summaries.

It does **not** perform dropout detection or correction itself.

**Parameters**

* None.

**Analysis output**

* Total number of dropouts
* Dropout counts per field
* Dropout size distributions (length and area)
* Line-based and field-based dropout density metrics

**Notes**

* Results depend on the quality of upstream dropout detection.
* Removing or adding dropouts upstream will directly affect analysis output.

---

## SNR Analysis Sink

| | |
|-|-|
| **Stage id** | `snr_analysis` |
| **Stage name** | SNR Analysis Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Produce signal-to-noise metrics for capture quality comparison |

**Use this stage when:**

* Comparing multiple captures of the same material
* Quantifying improvements from stacking or filtering
* Evaluating capture hardware or settings

**What it does**

This stage estimates signal-to-noise ratio (SNR) using spatial and/or temporal analysis of the incoming video stream. The exact method depends on the implementation but is designed to be consistent across comparable pipelines.

**Parameters**

* None.

**Analysis output**

* Overall SNR estimate
* Per-field or per-region SNR measurements (where applicable)
* Aggregate statistics suitable for cross-capture comparison

**Notes**

* Meaningful SNR comparison requires aligned sources.
* Use `source_align` and `stacker` appropriately upstream when comparing captures.

---

## Notes on Analysis Sink Stages

* Analysis sink stages terminate pipeline branches.
* Multiple analysis sinks may consume the same upstream output.
* Analysis sinks are side-effect-free with respect to media data.
* Results are intended for diagnostics, comparison, and validation—not for further pipeline processing.

This document complements the source-stage, transform-stage, and sink-stage documentation and provides a reference for decode-orc diagnostic workflows :contentReference[oaicite:0]{index=0}.
