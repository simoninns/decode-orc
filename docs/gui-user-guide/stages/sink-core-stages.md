# Sink (Core) Stages

Sink core stages are the **endpoints of a decode-orc pipeline**. They consume processed data from upstream stages and write results to disk. Unlike transform stages, sink stages do not produce outputs that can be connected further downstream.

A pipeline may contain **multiple sink stages** in parallel, allowing the same processed stream to be written in different formats or to different destinations.

Sink core stages are used to:

* Write final video outputs (TBC + metadata, CVBS files, or encoded video)
* Export auxiliary data such as audio, EFM, AC3, or closed captions
* Export intermediate data for inspection or external tools

---

## AC3 RF Sink

| | |
|-|-|
| **Stage id** | `AC3RFSink` |
| **Stage name** | AC3 RF Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Decode AC3 RF (Dolby Digital) samples and write AC3 frames to file |

**Use this stage when:**

* Processing later North American NTSC LaserDiscs that carry AC3 RF 5.1 surround sound
* You want the extracted AC3 audio alongside the video output in a single pipeline trigger

**What it does**

This stage reads AC3 RF samples from the incoming stream, decodes the RF-modulated Dolby Digital bitstream frame by frame, and writes the resulting AC3 audio frames sequentially to the output file. The output is a raw AC3 elementary stream with no container wrapping; it can be played back directly or muxed into a video container.

**Parameters**

* `output_path` (string)
    - Path to the output AC3 file. The conventional extension is `.ac3`.
    - Required.

**Notes**

* The upstream source must supply AC3 RF data; the pipeline will abort at trigger time if none is present.
* This stage is specific to AC3 RF as found on LaserDiscs; it does not handle AC3 carried in other formats or containers.

---

## Analogue Audio Sink

| | |
|-|-|
| **Stage id** | `AudioSink` |
| **Stage name** | Analogue Audio Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Export analogue audio to a WAV file |

**Use this stage when:**

* Your source contains analogue audio tracks
* You want to export audio independently of video output
* You want to inspect or process audio externally

**What it does**

This stage extracts analogue audio samples from the incoming stream and writes them to a standard WAV file. Audio remains synchronised to the processed video timeline, so any frame trimming or reordering performed upstream is reflected in the output.

The pipeline carries audio frame-locked to the video. For PAL (25 fps) the locked rate is exactly 44,100 Hz. For NTSC and PAL-M (30000/1001 fps) the locked rate is 44100000/1001 Hz ≈ 44,055.94 Hz, and the `sample_rate_mode` parameter selects how that audio is exported.

**Parameters**

* `output_path` (string)
    - Path to the output WAV file.
    - Required.

* `sample_rate_mode` (string, NTSC/PAL-M projects only)
    - `locked_44056` (default) — writes the frame-locked samples unmodified; the WAV header declares 44,056 Hz. No resampling; samples remain bit-identical to the pipeline audio.
    - `free_running_44100` — resamples the audio (SoXR, HQ) to standard free-running 44,100 Hz for tools that expect the standard rate. Duration and A/V sync are preserved.
    - Not shown for PAL projects, whose locked audio is already at 44,100 Hz.

**Notes**

* This stage handles analogue audio only. Digital audio carried as EFM (CD-quality stereo) or AC3 RF (Dolby Digital) must be extracted with the EFM Decoder Sink or AC3 RF Sink stages respectively.
* Audio stacking or selection must be performed upstream (e.g. via `stacker`).

---

## Closed Caption Sink

| | |
|-|-|
| **Stage id** | `CCSink` |
| **Stage name** | Closed Caption Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Extract and write NTSC Line 21 closed-caption (CC) data |

**Use this stage when:**

* Working with NTSC sources containing Line 21 closed captions
* You want to extract captions for archival or conversion
* You want to inspect CC data independently of video

**What it does**

For each field the stage reads the two caption bytes embedded in VBI Line 21, accumulates the byte pairs across the full field sequence, and writes them in the chosen format: Scenarist SCC V1.0 (industry-standard, with HH:MM:SS:FF timestamps and hex byte pairs) or plain text (printable ASCII only, control codes stripped).

**Parameters**

* `output_path` (string)
    - Path to the closed-caption output file. Use `.scc` for SCC format or `.txt` for plain text.
    - Required.

* `format` (string)
    - Export format.
    - Allowed values: `Scenarist SCC`, `Plain Text`.
    - Default: `Scenarist SCC`.

**Notes**

* Handles NTSC Line 21 only; PAL sources do not carry Line 21 CC data.
* CC data must be preserved upstream — masking Line 21 before this stage will destroy the caption payload.
* If the source contains no CC data the output file will be empty but the stage will not abort.

---

## CVBS Sink

| | |
|-|-|
| **Stage id** | `CVBSSink` |
| **Stage name** | CVBS Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Write CVBS frames to a CVBS file-format family output |

**Use this stage when:**

* You want to archive or exchange a processed CVBS signal in the standard CVBS file format.
* You want to produce a `.composite` or `.y`/`.c` output that can be re-opened by the CVBS Source stage.
* You need to write associated dropout, audio, EFM, or AC3 sidecars alongside the video.

**What it does**

This stage writes processed frame data using the selected sample encoding, and a `.meta` SQLite sidecar. The output signal type follows the project type automatically: a composite project is written as a single `.composite` file and a Y/C project as a `.y`/`.c` pair (per the CVBS file format naming convention) — Y/C cannot be derived from a composite signal, so this is not a choice. The `.meta` file records the signal type and the selected `sample_encoding_preset`, and always carries `signal_state_preset = 'STANDARD_TBC_LOCKED'`. The signal state is not user-configurable — it reflects the pipeline invariant that only locked, standard-state signals appear at this point.

Associated sidecars are written automatically when the upstream source provides them:

- `.dropouts.meta` — when dropout hints are present
- `_audio_00.wav` — when audio is present
- `.efm` + `.efm.meta` — when EFM data is present
- `.ac3` + `.ac3.meta` — when AC3 RF data is present

A CVBS file written by this stage can be round-tripped back through the CVBS Source stage.

**Parameters**

* `output_path` (string)
    - Base path for output files. A trailing `.composite`, `.y`, or `.c` extension is stripped when present.
    - Required.

* `sample_encoding` (string)
    - Sample encoding of the output data, recorded as `sample_encoding_preset` in the `.meta` file.
    - Allowed values: `CVBS_U10_4FSC`, `CVBS_U16_4FSC`, `CVBS_TPG21_4FSC`, `CVBS_S16_FSC`.
    - Default: `CVBS_U10_4FSC` (lossless; preserves headroom). The other encodings clamp to their representable domain before scaling.

* `capture_notes` (string)
    - Optional free-text notes written to the `.meta` file.
    - Default: `""` (not written when empty).

**Notes**

* `signal_state_preset` in the output `.meta` is always `STANDARD_TBC_LOCKED` and cannot be overridden by the user.
* Absent upstream extensions (no audio, no EFM, etc.) produce no sidecar files — this is not an error.

---

## EFM Decoder Sink

| | |
|-|-|
| **Stage id** | `EFMSink` |
| **Stage name** | EFM Decoder Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Decode EFM t-values to audio WAV or ECMA-130 binary sector data |

**Use this stage when:**

* Extracting digital audio from a LaserDisc source as a WAV file
* Extracting ECMA-130 data sectors from a LaserDisc source
* You want the fully decoded output of the EFM stream rather than the raw t-values

**What it does**

This stage accumulates EFM t-values from the incoming stream and runs the full EFM decode pipeline (demodulation, error detection, CIRC error correction, de-interleaving), producing either a standard PCM audio WAV file or ECMA-130 binary sector data depending on the chosen decode mode.

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
    - Disable interpolation-based audio error concealment. When disabled, affected samples are zeroed instead of interpolated.
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
* Audio and data decoding are mutually exclusive — select `decode_mode` before enabling mode-specific parameters. Parameters for the inactive mode are silently ignored.
* EFM stacking or correction should be performed upstream before this stage.

---

## FFmpeg Video Sink

| | |
|-|-|
| **Stage id** | `ffmpeg_video_sink` |
| **Stage name** | FFmpeg Video Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Chroma-decode and encode the processed video to a compressed file (H.264/MP4 or FFV1/MKV) |

**Use this stage when:**

* You want a playable, distributable, or archival video file
* You want optional embedded audio, closed captions, or chapter metadata

**What it does**

Applies the selected chroma decoder to convert the incoming TBC video stream to colour video, then encodes it using FFmpeg into the chosen container and codec. Optionally embeds analogue audio, closed captions (as mov_text subtitles, MP4 only), and chapter markers derived from VBI data.

**Parameters**

* `output_path` (string)
    - Output file path. Use `.mp4` for H.264 or `.mkv` for FFV1.
    - Required.

* `decoder_type` (string)
    - Chroma decoder to apply. PAL: `pal2d`, `transform2d`, `transform3d`. NTSC: `ntsc1d`, `ntsc2d`, `ntsc3d`, `ntsc3dnoadapt`. Other: `mono`.

* `output_format` (string)
    - Container and codec. Values: `mp4-h264` (H.264 in MP4), `mkv-ffv1` (FFV1 lossless in MKV).

* `chroma_gain` (double)
    - Chroma gain multiplier applied before encoding. Range: 0.0–10.0. Default: 1.0.

* `chroma_phase` (double)
    - Chroma phase rotation in degrees. Range: -180 to 180. Default: 0.

* `luma_nr` (int) / `chroma_nr` (int)
    - Luma / chroma noise reduction levels. Higher values reduce noise at the cost of sharpness or chroma resolution.

* `ntsc_phase_comp` (bool)
    - Enable NTSC phase compensation. NTSC sources only.

* `simple_pal` (bool)
    - Enable simple PAL chroma decoding. PAL sources only.

* `threads` (int)
    - Number of worker threads. Default: auto (all available cores).

* `output_padding` (int)
    - Padding added for codec alignment requirements. Default: 8.

* `encoder_preset` (string)
    - Encoder speed/quality trade-off. Values: `fast`, `medium`, `slow`, `veryslow`.

* `encoder_crf` (int)
    - Constant Rate Factor for quality-based encoding. Range: 0–51 (lower = higher quality). Default: 18. Used when `encoder_bitrate` is 0.

* `encoder_bitrate` (int)
    - Target bitrate in bits per second. When non-zero, overrides CRF mode. Default: 0 (use CRF).

* `embed_audio` (bool)
    - Embed analogue audio into the output file. Requires audio in the pipeline. Default: `false`.

* `embed_closed_captions` (bool)
    - Embed closed captions as mov_text subtitles. MP4 output only. Default: `false`.

* `embed_chapter_metadata` (bool)
    - Write chapter markers derived from VBI data into the output file. Default: `false`.

**Stage tools**

* **FFmpeg Preset Config** — a preset helper dialog that applies well-tested decoder/encoder combinations for PAL or NTSC sources without setting each parameter manually.

**Notes**

* CRF and bitrate modes are mutually exclusive; set `encoder_bitrate` to a non-zero value to switch from CRF mode.
* Uses the same chroma decoders as the Raw Video Sink.

---

## ld-decode Sink

| | |
|-|-|
| **Stage id** | `ld_sink` |
| **Stage name** | ld-decode Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Write an ld-decode-compatible TBC and metadata output |

**Use this stage when:**

* Producing final archival-quality outputs
* Feeding results back into the ld-decode ecosystem (ld-chroma-decoder, ld-analyse, ld-process-vbi, …)
* Preserving full per-field metadata

**What it does**

This stage writes:

* A `.tbc` file containing processed video fields
* A `.tbc.db` metadata database compatible with ld-decode

The output can be used directly with existing ld-decode tools.

**Parameters**

* `output_path` (string)
    - Base path for the output files — the stage appends the `.tbc` and `.tbc.db` extensions automatically.
    - Required.

**Notes**

* This is the most common "final output" sink stage.
* All upstream corrections, stacking, and parameter overrides should be complete before this stage.
* The target directory must exist and be writable at trigger time.
* This stage writes video and metadata only — export analogue audio, EFM, or AC3 RF data with the Analogue Audio Sink, Raw EFM Data Sink / EFM Decoder Sink, or AC3 RF Sink stages connected in parallel.

---

## Raw EFM Sink

| | |
|-|-|
| **Stage id** | `RawEFMSink` |
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
* This stage does not modify or decode EFM data. Use the EFM Decoder Sink stage to decode t-values to audio or sector data.

---

## Raw Video Sink

| | |
|-|-|
| **Stage id** | `raw_video_sink` |
| **Stage name** | Raw Video Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Chroma-decode the processed video and write uncompressed RGB/YUV/Y4M files |

**Use this stage when:**

* You need an uncompressed output for external tools such as FFmpeg, VirtualDub, or image-processing scripts
* You want direct integration with an image-processing pipeline

**What it does**

Applies the selected chroma decoder to convert the incoming TBC video stream to colour video, then writes the raw decoded frames to a file without compression. The output format determines the pixel layout and whether a Y4M header is prepended.

**Parameters**

* `output_path` (string)
    - Output file path. Use `.rgb`, `.yuv`, or `.y4m` to match the chosen format.
    - Required.

* `decoder_type` (string)
    - Chroma decoder to apply. PAL: `pal2d`, `transform2d`, `transform3d`. NTSC: `ntsc1d`, `ntsc2d`, `ntsc3d`, `ntsc3dnoadapt`. Other: `mono`.

* `output_format` (string)
    - Raw output format. Values: `rgb` (RGB48, 16-bit per channel), `yuv` (YUV444P16, planar), `y4m` (YUV444P16 with Y4M header).

* `chroma_gain` (double) / `chroma_phase` (double)
    - Chroma gain multiplier (0.0–10.0, default 1.0) and phase rotation in degrees (-180 to 180, default 0).

* `luma_nr` (int) / `chroma_nr` (int)
    - Luma / chroma noise reduction levels.

* `ntsc_phase_comp` (bool)
    - Enable NTSC phase compensation. NTSC sources only.

* `simple_pal` (bool)
    - Enable simple PAL chroma decoding. PAL sources only.

* `threads` (int)
    - Number of worker threads. Default: auto (all available cores).

* `output_padding` (int)
    - Alignment padding added to each output frame. Default: 8.

**Notes**

* This sink does not support audio embedding — use the FFmpeg Video Sink if you need audio in the output.
* Raw output files can be very large; ensure sufficient disk space before triggering.
* The `y4m` format is directly readable by tools such as FFmpeg and rav1e without specifying the pixel format manually.

---

## Notes on Sink Stages

* Sink stages terminate pipeline branches.
* Multiple sink stages may consume the same upstream output.
* Sink stages do not alter timing or metadata beyond their specific export role.
