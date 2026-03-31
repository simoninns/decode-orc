# Source stages

Source stages are the **starting point of every decode-orc pipeline**. They load captured video (and any associated audio or disc data) from disk and make it available for processing.

You normally use **one source stage per capture**. If you have multiple captures of the same material, you add multiple source stages and combine them later using transform stages.

Source stages do not improve or modify the signal. Their purpose is to:

* Load captured files correctly
* Validate the video system (PAL or NTSC)
* Keep video, audio, and disc data synchronised

Note that all metadata is expected in SQLite format (with .tbc.db).  Legacy JSON metadata is accepted with a warning, but lacks the newer fields that Decode-Orc expects.

### Composite vs Y/C Sources

* **Composite sources** are used for LaserDisc and other formats where luminance and chrominance are combined in a single signal.
* **Y/C sources** are used for colour-under formats (such as VHS and S-VHS) where luminance (Y) and chrominance (C) were captured separately.

You must choose the source stage that matches how your capture was produced.

---

## PAL Composite Source

| | |
|-|-|
| **Stage id** | `pal_composite_source` |
| **Stage name** | PAL Composite Source |
| **Connections** | No inputs → 1 output |
| **Purpose** | Load PAL composite TBC file and associated metadata |

**Use this stage when:**

* Your capture comes from a PAL or PAL-M LaserDisc
* You decoded the RF capture using ld-decode or encode-orc into a `.tbc` file

**What it does**

This stage loads a PAL composite TBC file and its associated metadata. If present, analogue audio and EFM disc data are attached so they remain synchronised throughout the pipeline.

**User-facing inputs**

* **TBC file** (`.tbc`)
* **PCM audio file** (optional)
* **EFM data file** (optional)

---

## NTSC Composite Source

| | |
|-|-|
| **Stage id** | `ntsc_composite_source` |
| **Stage name** | NTSC Composite Source |
| **Connections** | No inputs → 1 output |
| **Purpose** | Load NTSC composite TBC file and associated metadata |

**Use this stage when:**

* Your capture comes from an NTSC LaserDisc

**What it does**

This stage performs the same role as the PAL Composite Source, but enforces NTSC timing and field structure.

**User-facing inputs**

* **TBC file** (`.tbc`)
* **PCM audio file** (optional)
* **EFM data file** (optional)

---

## PAL Y/C Source

| | |
|-|-|
| **Stage id** | `pal_yc_source` |
| **Stage name** | PAL Y/C Source |
| **Connections** | No inputs → 1 output |
| **Purpose** | Load separate PAL luma and chroma files from colour-under format |

**Use this stage when:**

* Your capture comes from a PAL colour-under format
* Luminance and chrominance were captured as separate files

**What it does**

This stage combines separate luma (Y) and chroma (C) captures into a single video source while preserving higher chroma quality than composite decoding.

**User-facing inputs**

* **Luma (Y) file** (`.tbcy`)
* **Chroma (C) file** (`.tbcc`)
* **Metadata database** (optional)
* **PCM audio file** (optional)
* **EFM data file** (optional)

---

## NTSC Y/C Source

| | |
|-|-|
| **Stage id** | `ntsc_yc_source` |
| **Stage name** | NTSC Y/C Source |
| **Connections** | No inputs → 1 output |
| **Purpose** | Load separate NTSC luma and chroma files from colour-under format |

**Use this stage when:**

* Your capture comes from an NTSC colour-under format

**What it does**

This stage performs the same function as the PAL Y/C Source but enforces NTSC timing.

**User-facing inputs**

* **Luma (Y) file** (`.tbcy`)
* **Chroma (C) file** (`.tbcc`)
* **Metadata database** (optional)
* **PCM audio file** (optional)
* **EFM data file** (optional)

---