# Source stages

Source stages are the **starting point of every decode-orc pipeline**. They load captured video (and any associated audio or disc data) from disk and make it available for processing.

You normally use **one source stage per capture**. If you have multiple captures of the same material, you add multiple source stages and combine them later using transform stages.

Source stages do not improve or modify the signal. Their purpose is to:

* Load captured files correctly
* Validate the video system (PAL, NTSC, or PAL-M)
* Keep video, audio, and disc data synchronised

> **Project format note:** Decode-Orc 2.0 requires that every project declares a `video_format` (PAL, NTSC, or PAL-M) and a `source_format` (Composite or YC) at creation time. These fields are read-only after the project is created. The stage picker shows only source stages that match the declared format.

---

## TBC Source

| | |
|-|-|
| **Stage id** | `tbc_source` |
| **Stage name** | *derived from metadata at load time* (e.g. `PAL TBC Composite`, `NTSC TBC YC`) |
| **Connections** | No inputs → 1 output |
| **Purpose** | Load TBC files produced by ld-decode or vhs-decode |

**Use this stage when:**

* Your capture comes from a LaserDisc or colour-under tape format
* You decoded the RF capture using ld-decode or vhs-decode into `.tbc` files

**What it does**

This stage reads one or more TBC files, detects the video system and signal type (composite or Y/C) from the `.tbc.db` metadata database, and assembles full-frame CVBS_U10_4FSC buffers for downstream processing. The stage display name is resolved at load time from the metadata (`PAL TBC Composite`, `NTSC TBC YC`, etc.).

All TBC level values are remapped from the ld-decode/vhs-decode internal 16-bit domain to the CVBS_U10_4FSC 10-bit domain. PAL frames have exactly 709,379 samples; NTSC frames have 477,750 samples; PAL-M frames have 477,225 samples.

Associated audio (analogue `.pcm`), EFM disc data, and AC3 RF are attached automatically if present alongside the `.tbc` file.

**Composite variant user-facing inputs**

* **TBC file** (`.tbc`)
* Accompanying metadata database (`.tbc.db`)
* PCM audio file (optional, auto-detected)
* EFM data file (optional, auto-detected)

**Y/C variant user-facing inputs**

* **Luma (Y) file** (`.tbcy`)
* **Chroma (C) file** (`.tbcc`)
* Accompanying metadata database (auto-detected)
* PCM audio file (optional, auto-detected)
* EFM data file (optional, auto-detected)

**Notes**

* The stage validates that the Y/C colour-frame phase is aligned at open time. Misaligned Y/C files are rejected with a clear error.
* NTSC-J sources with a non-standard black level are detected automatically from metadata and exposed via a per-frame black level override.
* Legacy `.tbc.json` metadata produced by older ld-decode/vhs-decode versions is accepted with a warning; re-decoding with a current version (which produces `.tbc.db`) is recommended.

---

## CVBS Source

| | |
|-|-|
| **Stage id** | `PAL_CVBS_Source`, `NTSC_CVBS_Source`, or `PAL_M_CVBS_Source` (one variant per video system; the stage picker offers the one matching the project) |
| **Stage name** | CVBS Source |
| **Connections** | No inputs → 1 output |
| **Purpose** | Load CVBS captures stored in the CVBS file-format family |

**Use this stage when:**

* Your source is a CVBS file (`.composite`, or a `.y`/`.c` pair for Y/C projects) rather than a TBC capture

**What it does**

This stage reads CVBS payloads from `.composite` files (or `.y`/`.c` pairs) and normalises them to the CVBS_U10_4FSC 10-bit domain. By default the video system, sample encoding, and signal state are read from the `.meta` SQLite sidecar; because the CVBS file format declares metadata optional, the sample encoding can also be selected manually so that sources without a sidecar can be used.

Only the `STANDARD_TBC_LOCKED` signal-state preset is accepted. Files with any other signal state are rejected with a clear error before any frame data is returned. When a sample encoding is selected manually the sidecar is ignored: the signal is assumed to be TBC-locked, the frame count is measured from the file size, and any audio sidecar is treated as free-running.

The following sample encodings are normalised automatically:

| Encoding | Normalisation |
|----------|---------------|
| `CVBS_U10_4FSC` | Identity (already 10-bit) |
| `CVBS_U16_4FSC` | `value = uint16_value / 64` |
| `CVBS_TPG21_4FSC` | `value = int16_value / 64 + 508` |
| `CVBS_S16_FSC` | `value = int16_value / 32 + blanking_10bit` |

Associated dropout, audio, EFM, and AC3 sidecars are loaded automatically if present.

**Parameters**

The file-path parameters offered match the project's source type: a composite project shows only the CVBS file path, while a Y/C project shows only the Y (luma) and C (chroma) paths.

* `input_path` (file path)
    - Path to the composite data file (`.composite`). Composite projects only.

* `y_path` / `c_path` (file paths)
    - Paths to the luma (`.y`) and chroma (`.c`) channel files. Y/C projects only; set together.

* `sample_encoding` (string)
    - `From metadata` (default) reads the encoding from the `.meta` sidecar.
    - Selecting `CVBS_U10_4FSC`, `CVBS_U16_4FSC`, `CVBS_TPG21_4FSC`, or `CVBS_S16_FSC` manually makes the sidecar optional.

**Notes**

* Colour-frame index (PAL: 1–4, NTSC: 0–1, PAL-M: 1–4) is measured from the colour burst on each frame and stored in the frame descriptor. Frames where the burst is absent or unmeasurable carry `colour_frame_index = -1`.
