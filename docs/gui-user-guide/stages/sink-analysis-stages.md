# Sink (Analysis) Stages

Analysis sink stages are **terminal stages** that generate diagnostics, metrics, and reports rather than producing media or hardware output. They consume processed data from upstream stages and emit **analysis results** intended for comparison, validation, or debugging.

Analysis sinks:

* Do not modify video, audio, or metadata
* Do not produce outputs that can be connected further downstream
* Display results in a chart window and can optionally write a CSV file

They are typically used to:

* Compare capture quality across multiple sources
* Validate signal stability and decode quality
* Quantify the effects of transform stages such as stacking or dropout correction

All three analysis sinks work the same way: trigger the stage to compute the dataset, after which the matching analysis chart dialog opens automatically. The dataset is cached in the stage and the chart can be re-opened at any time from the **Stage Tools** menu.

---

## Burst Level Analysis Sink

| | |
|-|-|
| **Stage id** | `burst_level_analysis_sink` |
| **Stage name** | Burst Level Analysis Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Measure colour burst level stability across fields |

**Use this stage when:**

* Evaluating chroma signal stability
* Comparing multiple captures of the same source
* Diagnosing colour amplitude fluctuations or capture issues

**What it does**

This stage measures the amplitude of the colour burst for each field and generates statistics describing burst level variation over time (per-field measurements plus aggregate mean, variance, min/max). After triggering, the Burst Level Analysis chart is opened automatically.

**Parameters**

* `output_path` (file path)
    - Destination CSV file for burst metrics. Leave empty to skip file output.
* `write_csv` (bool)
    - Enable writing results to CSV at trigger time.

**Stage tools**

* **Burst Level Analysis** — displays per-frame colour-burst amplitude measurements in a chart window. Invoked automatically after triggering; can be re-opened from the Stage Tools menu.

**Notes**

* Results are meaningful only if colour burst timing is correct upstream.
* Masking or altering the burst region before this stage will invalidate results.
* Connect one instance before and one after the Stacker stage to compare burst stability across captures.

---

## Dropout Analysis Sink

| | |
|-|-|
| **Stage id** | `dropout_analysis_sink` |
| **Stage name** | Dropout Analysis Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Produce statistics describing dropout frequency, size, and distribution |

**Use this stage when:**

* Comparing dropout levels between captures
* Evaluating the effectiveness of stacking or dropout correction
* Identifying problematic regions of a capture

**What it does**

This stage reads dropout hints present in the stream (originating from the source or modified by transform stages such as `dropout_map`) and generates statistical summaries: total dropout count, per-field counts, size distributions, and line/field density metrics. After triggering, the Dropout Analysis chart is opened automatically.

It does **not** perform dropout detection or correction itself.

**Parameters**

* `output_path` (file path)
    - Destination CSV file for dropout metrics. Leave empty to skip file output.
* `write_csv` (bool)
    - Enable writing results to CSV at trigger time.

**Stage tools**

* **Dropout Analysis** — displays dropout frequency, size, and distribution charts. Invoked automatically after triggering; can be re-opened from the Stage Tools menu.

**Notes**

* Results depend on the quality of upstream dropout detection.
* Removing or adding dropouts upstream will directly affect analysis output.
* Connect one instance before the Stacker and one after to see the dropout reduction achieved by stacking.

---

## SNR Analysis Sink

| | |
|-|-|
| **Stage id** | `snr_analysis_sink` |
| **Stage name** | SNR Analysis Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Produce signal-to-noise metrics for capture quality comparison |

**Use this stage when:**

* Comparing multiple captures of the same material
* Quantifying improvements from stacking or filtering
* Evaluating capture hardware or settings

**What it does**

This stage estimates signal-to-noise ratio using spatial and temporal analysis of the incoming video stream, reporting **white SNR** and **black SNR** per field along with aggregate statistics. Results are consistent across comparable pipelines, allowing meaningful cross-capture comparison. After triggering, the SNR Analysis chart is opened automatically.

**Parameters**

* `output_path` (file path)
    - Destination CSV file for SNR metrics. Leave empty to skip file output.
* `write_csv` (bool)
    - Enable writing results to CSV at trigger time.

**Stage tools**

* **SNR Analysis** — displays white SNR and black SNR metrics over time in a chart window. Invoked automatically after triggering; can be re-opened from the Stage Tools menu.

**Notes**

* Meaningful SNR comparison requires aligned sources.
* Use `source_align` and `stacker` appropriately upstream when comparing captures.
* Stacking improves SNR only where sources contain independent noise; identical sources will not show an SNR improvement on dropout-free areas.

---

## Notes on Analysis Sink Stages

* Analysis sink stages terminate pipeline branches.
* Multiple analysis sinks may consume the same upstream output.
* Analysis sinks are side-effect-free with respect to media data.
* Results are intended for diagnostics, comparison, and validation—not for further pipeline processing.
