# Sink (3rd Party) Stages

Sink 3rd party stages are the **endpoints of a decode-orc pipeline** targeting external tools (external from the ld-decode and vhs-decode projects). They consume processed data from upstream stages and write results to disk or hardware. Unlike transform stages, sink stages do not produce outputs that can be connected further downstream.

A pipeline may contain **multiple sink stages** in parallel, allowing the same processed stream to be written in different formats or to different destinations.

Sink core stages are used to:

* Write final video outputs (TBC + metadata)
* Export auxiliary data such as audio, EFM, or closed captions
* Output video directly to hardware for monitoring or capture
* Export intermediate data for inspection or external tools

---

## HackDAC Sink

| | |
|-|-|
| **Stage id** | `hackdac_sink` |
| **Stage name** | HackDAC Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Output video directly to HackDAC hardware |

**Use this stage when:**

* You want real-time analogue video output
* Monitoring pipeline output on CRT or analogue equipment
* Testing signal characteristics on real hardware

**What it does**

This stage streams processed video fields directly to connected HackDAC hardware.

**Parameters**

* Hardware-specific parameters may be supported depending on build and platform.
* Typical configurations are provided externally rather than via stage parameters.

**Notes**

* This stage is hardware-dependent.
* Timing and field order must already be correct upstream.

---

## NN NTSC Chroma Sink

| | |
|-|-|
| **Stage id** | `nn_ntsc_chroma_sink` |
| **Stage name** | NN NTSC Chroma Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Decode NTSC chroma using a neural-network 3D spatial/temporal comb filter |

**Use this stage when:**

* Processing NTSC composite or Y/C sources that benefit from a deep-learning–based chroma decoder
* The standard NTSC chroma decoders (`ntsc2d` / `ntsc3d`) leave residual cross-colour or dot-crawl artefacts
* You want a fully self-contained raw video export (RGB48, YUV444P16, or Y4M) without FFmpeg encoding
* You are comparing NN-based chroma separation against the conventional comb-filter decoders on the same source material

**What it does**

This stage applies the `nnNtscTransform3D` neural-network chroma decoder to every NTSC frame in the pipeline. For each frame the stage:

1. Extracts a ±1-frame (4-field) temporal window from the `VideoFieldRepresentation`
2. Tiles each field into 16×16×4 overlapping blocks with a Hann window
3. Runs a 3D complex FFT (FFTW3) on each block
4. Feeds the spectral magnitudes into an embedded ONNX model (`chroma_net_v2`) that predicts a per-frequency chroma gain mask
5. Applies the mask, runs the inverse FFT, and overlap-adds the result back into a full chroma estimate
6. Demodulates I/Q, applies optional noise reduction and phase/gain correction, rotates I/Q to U/V
7. Writes each decoded frame to the output file in the chosen format

The ONNX model weights are compiled directly into the binary; no external model file is required at runtime.

**Parameters**

* `output_path` (string)
    - Path to the output video file. The extension determines the human-readable format but the actual data format is controlled by `output_format`.
    - Required.

* `output_format` (string)
    - `rgb` — RGB48: 16 bits per channel, planar interleaved R→G→B per row. Compatible with FFmpeg `-pixel_format rgb48le`.
    - `yuv` — YUV444P16: 16-bit planar Y, U, V. Compatible with FFmpeg `-pixel_format yuv444p16le`.
    - `y4m` — YUV444P16 prefixed with a Y4M (YUV4MPEG2) stream header. Readable by mpv and FFmpeg directly without specifying pixel format or resolution.
    - Default: `rgb`.

* `threads` (integer)
    - Number of worker threads for parallel frame decoding.
    - `0` = auto-detect (capped at 4 to avoid saturating the system during ONNX inference; see *Notes*).
    - Range: 0–64.
    - Default: `0`.

* `chroma_gain` (float)
    - Multiplicative gain applied to the decoded chroma signal after IQ→UV rotation.
    - Increase above `1.0` to boost undersaturated colours; decrease below `1.0` to tame oversaturation.
    - Range: 0.0–10.0.
    - Default: `1.0`.

* `chroma_phase` (float)
    - Phase rotation in degrees applied during IQ→UV conversion.
    - Use this to correct a consistent hue shift in the decoded output.
    - Range: −180° to +180°.
    - Default: `0.0`.

* `deterministic_output` (boolean)
    - Forces single-threaded processing, producing bit-identical output across runs regardless of CPU or thread scheduling.
    - Overrides the `threads` parameter.
    - Use when verifying the decoder against a reference or comparing runs of the same source.
    - Default: `false`.

* `chroma_nr` (float)
    - Chroma noise reduction level. `0.0` disables chroma NR.
    - Range: 0.0–100.0.
    - Default: `0.0`.

* `luma_nr` (float)
    - Luma noise reduction level. `0.0` disables luma NR.
    - Range: 0.0–100.0.
    - Default: `0.0`.

**Notes**

* **NTSC only.** The stage rejects PAL and PAL-M sources at trigger time with a clear error message.
* **Thread cap.** The default auto-thread mode caps at 4 workers because ONNX CPU inference is compute-intensive. Running one worker per hardware thread (e.g. 16 or 32 workers) causes Linux desktop environments to become unresponsive during long encodes. Raise `threads` manually only if you have confirmed headroom.
* **Multi-threaded output order.** When `threads > 1` the tile-level FFTW operations are non-deterministic across runs (tile overlap-add ordering is thread-dependent). Enable `deterministic_output` for reproducible bit-exact results.
* **Preview.** The stage supports live preview in the GUI: after the pipeline is executed, click the stage node to open the preview panel. Live tweaks for `chroma_gain`, `chroma_phase`, `luma_nr`, and `chroma_nr` are applied interactively without re-triggering the full encode.
* **Memory.** The entire field buffer for the source is held in memory during decoding. For very long captures this may be several gigabytes. Ensure the system has sufficient RAM before triggering.

**Comparing with standard NTSC chroma decoders**

| | NN NTSC Chroma Sink | Chroma Sink (`ntsc3d`) |
|---|---|---|
| Algorithm | Neural-network 3D comb (ONNX) | Adaptive 3D comb filter |
| Output formats | RGB48 / YUV444P16 / Y4M | Configurable via FFmpeg |
| NTSC only | Yes | Yes |
| Reproducible | `deterministic_output=true` | Always deterministic |
| Stage category | 3rd Party (experimental) | Core |
| Preview | Yes (per-frame seek) | Yes |

Use the standard `chroma_sink` (ntsc3d) as your baseline. Add the NN NTSC Chroma Sink to compare quality on challenging material (fast motion, high-chroma scenes, or cross-colour-heavy captures).

---

## Daphne VBI Sink

TBA
