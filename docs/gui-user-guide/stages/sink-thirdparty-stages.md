# Sink (3rd Party) Stages

Sink 3rd party stages are the **endpoints of a decode-orc pipeline** targeting external tools (external from the ld-decode and vhs-decode projects). They consume processed data from upstream stages and write results to disk or hardware. Unlike transform stages, sink stages do not produce outputs that can be connected further downstream.

A pipeline may contain **multiple sink stages** in parallel, allowing the same processed stream to be written in different formats or to different destinations.

---

## CVBS Sink

| | |
|-|-|
| **Stage id** | `cvbs_sink` |
| **Stage name** | CVBS Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Write CVBS frames to a CVBS file-format family output |

**Use this stage when:**

* You want to archive or exchange a processed CVBS signal in the standard CVBS file format.
* You want to produce a `.composite` or `.y`/`.c` output that can be re-opened by the CVBS Source stage.
* You need to write associated dropout, audio, EFM, or AC3 sidecars alongside the video.

**What it does**

This stage writes processed CVBS_U10_4FSC frame data to a `.composite` file (or `.y`/`.c` pair for Y/C signal type) and a `.meta` SQLite sidecar. The `.meta` file always carries `sample_encoding_preset = 'CVBS_U10_4FSC'` and `signal_state_preset = 'STANDARD_TBC_LOCKED'`. These values are not user-configurable — they reflect the pipeline invariant that only locked, standard-state signals appear at this point.

Associated sidecars are written automatically when the upstream source provides them:
- `.dropouts.meta` — when dropout hints are present
- `_audio_00.wav` — when audio is present (`has_audio() == true`)
- `.efm` + `.efm.meta` — when EFM data is present
- `.ac3` + `.ac3.meta` — when AC3 RF data is present

A CVBS file written by this stage can be round-tripped back through the CVBS Source stage.

**Parameters**

* `output_path` (string)
    - Base path for output files (without extension).
    - Required.

* `signal_type` (string)
    - Signal type to write.
    - Allowed values: `composite`, `yc`.
    - Default: `composite`.

* `capture_notes` (string)
    - Optional free-text notes written to the `.meta` file.
    - Default: `""` (not written when empty).

**Notes**

* `signal_state_preset` in the output `.meta` is always `STANDARD_TBC_LOCKED` and cannot be overridden by the user.
* Absent upstream extensions (no audio, no EFM, etc.) produce no sidecar files — this is not an error.

---

## Daphne VBI Sink

TBA

---

## Removed stages

### HackDAC Sink (removed in v2.0)

The `hackdac_sink` stage was removed in Decode-Orc 2.0. It is no longer available in the plugin registry. Projects that referenced this stage must be recreated without it.
