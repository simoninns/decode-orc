# Sink (Core) Stages

Sink core stages are the **endpoints of a decode-orc pipeline**. They consume processed data from upstream stages and write results to disk or hardware. Unlike transform stages, sink stages do not produce outputs that can be connected further downstream.

A pipeline may contain **multiple sink stages** in parallel, allowing the same processed stream to be written in different formats or to different destinations.

Sink core stages are used to:

* Write final video outputs (TBC + metadata)
* Export auxiliary data such as audio, EFM, or closed captions
* Output video directly to hardware for monitoring or capture
* Export intermediate data for inspection or external tools

---

## Analogue Audio Sink

| | |
|-|-|
| **Stage id** | `audio_sink` |
| **Stage name** | Analogue Audio Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Export analogue audio to a WAV file |

**Use this stage when:**

* Your source contains analogue audio tracks
* You want to export audio independently of video output
* You want to inspect or process audio externally

**What it does**

This stage extracts analogue audio samples from the incoming stream and writes them to a standard WAV file. Audio remains synchronised to the processed video timeline.

**Parameters**

* `output` (string)
    - Path to the output WAV file.
    - Required.

**Notes**

* Digital audio (if present) is not handled by this stage.
* Audio stacking or selection must be performed upstream (e.g. via `stacker`).

---

## Chroma Sink

| | |
|-|-|
| **Stage id** | `chroma_sink` |
| **Stage name** | Chroma Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Export decoded chroma information for inspection or external processing |

**Use this stage when:**

* Debugging or validating chroma decoding
* Inspecting chroma independently of luma
* Feeding chroma data into external analysis tools

**What it does**

This stage writes chroma samples to disk in a raw format suitable for offline inspection or research workflows.

**Parameters**

* `output` (string)
    - Path to the chroma output file.
    - Required.

**Notes**

* The exact chroma format depends on the upstream decoding stages.
* This stage is primarily intended as a diagnostic / research tool.

---

## Closed Caption Sink

| | |
|-|-|
| **Stage id** | `cc_sink` |
| **Stage name** | Closed Caption Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Extract and write closed-caption (CC) data |

**Use this stage when:**

* Working with NTSC sources containing Line 21 closed captions
* You want to extract captions for archival or conversion
* You want to inspect CC data independently of video

**What it does**

This stage scans the incoming video stream for closed-caption data and writes decoded caption packets to a file.

**Parameters**

* `output` (string)
    - Path to the closed-caption output file.
    - Required.

**Notes**

* CC data must be preserved upstream (e.g. not masked out).
* Masking Line 21 before this stage will prevent caption extraction.

---

## EFM Decoder Sink

| | |
|-|-|
| **Stage id** | `efm_sink` |
| **Stage name** | EFM Decoder Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Decode EFM t-values to audio WAV or ECMA-130 binary sector data |

**Use this stage when:**

* Extracting digital audio from a LaserDisc source as a WAV file
* Extracting ECMA-130 data sectors from a LaserDisc source
* You want the fully decoded output of the EFM stream rather than the raw t-values

**What it does**

This stage accumulates EFM t-values from the incoming stream and runs the full EFM decode pipeline, producing either a standard PCM audio WAV file or ECMA-130 binary sector data depending on the chosen decode mode.

**Parameters**

* `output_path` (string)
    - Path to the decoded output file. Use `.wav` for audio mode or `.bin` for data mode.
    - Required.

* `decode_mode` (string)
    - Selects the decode target. `audio` (default) produces a WAV or raw PCM file; `data` produces ECMA-130 binary sector data.
    - Allowed values: `audio`, `data`.
    - Default: `audio`.

* `no_timecodes` (boolean)
    - Disable timecode verification (early discs did not include time-codes in the EFM and will fail to decode without this option).
    - Applies to both `audio` and `data` modes.
    - Default: `false`.

* `audacity_labels` (boolean)
    - Write an Audacity label file alongside the audio output indicating the position of chapters as well as any missing samples.
    - Applies only in `audio` mode.
    - Default: `false`.

* `no_audio_concealment` (boolean)
    - Disable interpolation-based audio error concealment.
    - Applies only in `audio` mode.
    - Default: `false`.

* `zero_pad` (boolean)
    - Zero-pad the start of audio output so the sample starts from 00:00:00.0 relative to the first valid time-code.
    - Applies only in `audio` mode.
    - Default: `false`.

* `no_wav_header` (boolean)
    - Output raw PCM samples without a WAV file header.
    - Applies only in `audio` mode.
    - Default: `false`.

* `output_metadata` (boolean)
    - Write a bad-sector map metadata file alongside the sector output.  This file contains the number of any missing or corrupt sectors.
    - Applies only in `data` mode.
    - Default: `false`.

* `report` (boolean)
    - Write a detailed decode statistics report file.
    - Default: `false`.

**Notes**

* The source stage must supply an EFM file; the pipeline will abort if no EFM data is present in the incoming stream.
* Audio and data decoding are mutually exclusive — select `decode_mode` before enabling mode-specific parameters.
* EFM stacking or correction should be performed upstream before this stage.

---

## LD Sink

| | |
|-|-|
| **Stage id** | `ld_sink` |
| **Stage name** | LD Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Write an ld-decode-compatible TBC and metadata output |

**Use this stage when:**

* Producing final archival-quality outputs
* Feeding results back into the ld-decode ecosystem
* Preserving full metadata, audio, and disc data

**What it does**

This stage writes:

* A `.tbc` file containing processed video fields
* A metadata database compatible with ld-decode
* Associated audio and EFM data if present

The output can be used directly with existing ld-decode tools.

**Parameters**

* `output` (string)
    - Base path for output files.
    - Required.

**Notes**

* This is the most common “final output” sink stage.
* All upstream corrections, stacking, and parameter overrides should be complete before this stage.

---

## Raw EFM Sink

| | |
|-|-|
| **Stage id** | `raw_efm_sink` |
| **Stage name** | Raw EFM Data Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Write raw EFM t-values to a binary file |

**Use this stage when:**

* Archiving LaserDisc EFM t-values for later processing
* Feeding raw EFM data into external decoding or analysis tools
* Verifying EFM integrity after stacking or correction

**What it does**

This stage extracts raw EFM (Eight-to-Fourteen Modulation) t-values from the incoming stream and writes them to a binary file. The output contains only 8-bit unsigned integers representing valid t-values in the range 3–11, stored field by field with no headers or additional formatting.

**Parameters**

* `output_path` (string)
    - Path to the output EFM file (raw t-values). Conventionally uses the `.efm` extension.
    - Required.

**Notes**

* The source stage must supply an EFM file; the pipeline will abort if no EFM data is present in the incoming stream.
* EFM stacking behaviour is controlled upstream (e.g. via `stacker`).
* This stage does not modify or decode EFM data. Use the `efm_sink` stage to decode t-values to audio or sector data.

---

## Notes on Sink Stages

* Sink stages terminate pipeline branches.
* Multiple sink stages may consume the same upstream output.
* Sink stages do not alter timing or metadata beyond their specific export role.
