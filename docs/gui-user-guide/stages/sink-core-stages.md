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

## Audio Sink

| | |
|-|-|
| **Stage id** | `AudioSink` |
| **Stage name** | Audio Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Export any pipeline audio channel pair to a WAV file |

**Use this stage when:**

* Your source carries audio channel pairs
* You want to export audio independently of video output
* You want to inspect or process audio externally

**What it does**

This stage extracts one audio channel pair from the incoming stream and writes it to a standard WAV file. The pair can be any channel pair carried by the pipeline — analogue capture audio, decoded EFM digital audio, an imported WAV, or a channel pair derived by a transform. Audio remains synchronised to the processed video timeline, so any frame trimming or reordering performed upstream is reflected in the output.

The pipeline carries stereo audio channel pairs at exactly 48,000 Hz, frame-locked (synchronous) to the video for every system, following SMPTE 272M-1994. The WAV output is 24-bit signed little-endian PCM declaring 48,000 Hz; no resampling or bit-depth conversion is performed.

**Parameters**

* `output_path` (string)
    - Path to the output WAV file.
    - Required.

* `channel_pair` (integer)
    - Audio channel pair to write, 0-based (0–7), matching the CVBS container's `_audio_<p>.wav` numbering.
    - Default 0. Triggering fails if the selected channel pair does not exist.

**Notes**

* This stage writes whatever channel pair you select. Analogue capture audio arrives as channel pair 0 from the source; EFM digital audio (CD-quality stereo) becomes a channel pair when you add an **EFM Audio Decode** transform upstream, after which it can be written here like any other pair. For a bit-exact, un-resampled WAV of EFM audio use the EFM Decoder Sink instead; AC3 RF (Dolby Digital) is exported via the AC3 RF Sink.
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
- `_audio_0.wav` … `_audio_7.wav` — when audio is present (one 24-bit 48 kHz stereo WAV per channel pair)
- `.efm` + `.efm.meta` — when EFM data is present
- `.ac3` + `.ac3.meta` — when AC3 RF data is present

A CVBS file written by this stage can be round-tripped back through the CVBS Source stage.

**Parameters**

* `output_path` (string)
    - Base path for output files. A trailing `.composite`, `.y`, or `.c` extension is stripped when present.
    - Required.

* `sample_encoding` (string)
    - Sample encoding of the output data, recorded as `sample_encoding_preset` in the `.meta` file.
    - Allowed values: `CVBS_U10_4FSC`, `CVBS_U16_4FSC`, `CVBS_TPG21_4FSC`, `CVBS_S16_4FSC`.
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

* `ignore_preemphasis` (boolean)
    - Ignore the 50/15 µs pre-emphasis CONTROL flag (IEC 60908 §17.5) and write the audio exactly as decoded. When unchecked (default), sections flagged as pre-emphasised are de-emphasised during decode with a 50/15 µs filter so the output plays back with a flat response; enable this only if you want the raw pre-emphasised samples. When `audacity_labels` is enabled, a pre-emphasised track's label reads `Preemphasis:50/15us(removed)` when de-emphasis was applied, or `Preemphasis:50/15us` when this flag is set.
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
* This stage writes video and metadata only — export analogue audio, EFM, or AC3 RF data with the Audio Sink, Raw EFM Data Sink / EFM Decoder Sink, or AC3 RF Sink stages connected in parallel.

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

## Video Sink

| | |
|-|-|
| **Stage id** | `video_sink` |
| **Stage name** | Video Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Chroma-decode the processed video and write it to a file, either FFmpeg-encoded (MP4/MKV/MOV/MXF) or uncompressed raw (RGB/YUV/Y4M) |

**Use this stage when:**

* You want a playable, distributable, or archival video file (FFmpeg mode)
* You want optional embedded audio, closed captions, or chapter metadata (FFmpeg mode)
* You need an uncompressed output for external tools such as FFmpeg, VirtualDub, or image-processing scripts (raw mode)

**What it does**

Applies the selected chroma decoder to convert the incoming TBC video stream to colour video, then writes the result according to the selected output mode. In FFmpeg mode the video is encoded into the chosen container and codec, optionally embedding pipeline audio (up to 8 channel pairs, one output stream per pair), closed captions (as mov_text subtitles, MP4/MOV only), and chapter markers derived from VBI data. In raw mode the decoded frames are written to a file without compression; the raw format determines the pixel layout and whether a Y4M header is prepended.

**Parameters**

* `output_path` (string)
    - Output file path. Match the extension to the selected mode and format: `.mp4`, `.mkv`, `.mov`, or `.mxf` for FFmpeg output; `.rgb`, `.yuv`, or `.y4m` for raw output.
    - Required.

* `decoder_type` (string)
    - Chroma decoder to apply. PAL: `pal2d`, `transform2d`, `transform3d`. NTSC: `ntsc1d`, `ntsc2d`, `ntsc3d`, `ntsc3dnoadapt`. Other: `mono`.

* `output_mode` (string)
    - Output path selection. Values: `ffmpeg` (encoded output via FFmpeg), `raw` (uncompressed file output). Default: `ffmpeg`.

* `raw_format` (string)
    - Raw output format (raw mode only). Values: `rgb` (RGB48, 16-bit per channel), `yuv` (YUV444P16, planar), `y4m` (YUV444P16 with Y4M header). Default: `rgb`.

* `ffmpeg_format` (string)
    - Container and codec (FFmpeg mode only). Values include `mp4-h264`, `mkv-ffv1`, `mov-prores`, `mov-v210`, `mov-v410`, `mxf-mpeg2video`, `mov-h264`, `mp4-hevc`, `mov-hevc`, and `mp4-av1`. Default: `mp4-h264`.

* `chroma_gain` (double) / `chroma_phase` (double)
    - Chroma gain multiplier (0.0–10.0, default 1.0) and phase rotation in degrees (-180 to 180, default 0).

* `luma_nr` (double) / `chroma_nr` (double)
    - Luma / chroma noise reduction levels. Higher values reduce noise at the cost of sharpness or chroma resolution.

* `ntsc_phase_comp` (bool)
    - Enable NTSC phase compensation. NTSC sources only.

* `simple_pal` (bool)
    - Enable simple PAL chroma decoding (1D UV filter for Transform PAL). `transform2d`/`transform3d` decoders only.

* `transform_threshold` (double)
    - Similarity threshold for the Transform PAL decoder. Higher = more transform filtering. Range: 0.0–1.0. Default: 0.4. `transform2d`/`transform3d` decoders only.

* `chroma_weight` (double)
    - Chroma weight for the NTSC 3D adaptive filter. Higher = prefer more 2D result. Range: 0.0–10.0. Default: 1.0. `ntsc3d`/`ntsc3dnoadapt` decoders only.

* `adapt_threshold` (double)
    - NTSC 3D adaptive filter threshold. Higher = prefer more 3D result. Range: 0.0–10.0. Default: 1.0. `ntsc3d` decoder only.

* `output_padding` (int)
    - Alignment padding added to each output frame. Default: 8.

* `encoder_preset` (string)
    - FFmpeg mode only. Encoder speed/quality trade-off. Values: `fast`, `medium`, `slow`, `veryslow`.

* `encoder_crf` (int)
    - FFmpeg mode only. Constant Rate Factor for quality-based encoding. Range: 0–51 (lower = higher quality). Default: 18. Used when `encoder_bitrate` is 0.

* `encoder_bitrate` (int)
    - FFmpeg mode only. Target bitrate in bits per second. When non-zero, overrides CRF mode. Default: 0 (use CRF).

* `hardware_encoder` (string)
    - FFmpeg mode only. Hardware-accelerated encoding backend. Values: `none`, `vaapi`, `nvenc`, `qsv`, `amf`, `videotoolbox`. Default: `none`.

* `prores_profile` (string)
    - FFmpeg mode only, `mov-prores` format. ProRes quality profile: `proxy`, `lt`, `standard`, `hq`, `4444`, `4444xq`. Default: `hq`.

* `use_lossless_mode` (bool)
    - FFmpeg mode only. Enable mathematically lossless encoding (H.264/H.265/AV1 only, overrides CRF). Default: `false`.

* `apply_deinterlace` (bool)
    - FFmpeg mode only. Apply bwdif deinterlacing for progressive web playback. One frame is produced per field, so the output frame rate doubles (50 fps PAL, 59.94 fps NTSC). Default: `false`.

* `display_aspect_ratio` (string)
    - FFmpeg mode only. Display aspect ratio signalled to players. Metadata only — the video is not rescaled. Values: `auto` (square pixels), `4:3`, `16:9`. Most SD material should be played back at `4:3`. Default: `auto`.

* `video_filter` (string)
    - FFmpeg mode only. Custom FFmpeg video filter chain applied before encoding, using the same syntax as ffmpeg's `-vf` option (e.g. `fieldmatch,decimate` for inverse telecine, `crop=692:554`). Filters may change output dimensions and frame rate; the encoder follows the filter output automatically. An invalid filter string fails the export with the FFmpeg error message. Default: empty (no filtering).

* `embed_audio` (bool)
    - FFmpeg mode only. Embed pipeline audio into the output file, one output audio stream per selected channel pair. Requires audio in the pipeline. Default: `false`.

* `audio_channel_pairs` (string)
    - FFmpeg mode only; available only when `embed_audio` is enabled. Which audio channel pairs to embed: `all` (default) or a comma-separated list of 0-based channel pair indices, e.g. `0,2`. Indices match the CVBS container's `_audio_<p>.wav` numbering. The export fails if a listed channel pair does not exist.

* `audio_gain_db` (double)
    - FFmpeg mode only; available only when `embed_audio` is enabled. Gain applied to the embedded audio in decibels. `0` = unchanged; positive boosts (6 dB roughly doubles the amplitude), negative attenuates. Samples are clipped at full scale. Range: -24 to 24. Default: `0`.

* `embed_closed_captions` (bool)
    - FFmpeg mode only. Embed closed captions as mov_text subtitles. MP4/MOV output only. Default: `false`.

* `embed_chapter_metadata` (bool)
    - FFmpeg mode only. Write chapter markers derived from VBI data into the output file. Default: `false`.

**Stage tools**

* **FFmpeg Preset Config** — a preset helper dialog that applies well-tested encoder combinations without setting each parameter manually. Applying a preset switches the stage to FFmpeg output mode.

**Notes**

* Raw mode does not support audio, closed caption, or chapter embedding; those options apply to FFmpeg output only.
* Raw output files can be very large; ensure sufficient disk space before triggering.
* The `y4m` raw format is directly readable by tools such as FFmpeg and rav1e without specifying the pixel format manually.
* CRF and bitrate modes are mutually exclusive; set `encoder_bitrate` to a non-zero value to switch from CRF mode.
* Video filtering (`apply_deinterlace` or `video_filter`) is not supported with hardware encoders that use GPU surfaces (`vaapi`, `qsv`, `videotoolbox`); the export automatically falls back to the software encoder in that case.
* When a video filter chain is active, interlaced coding flags are not forced on the encoder; the field structure of the filter output determines how frames are flagged.
* Projects created with the earlier separate `raw_video_sink` and `ffmpeg_video_sink` stages are migrated to this stage automatically when loaded.

---

## Notes on Sink Stages

* Sink stages terminate pipeline branches.
* Multiple sink stages may consume the same upstream output.
* Sink stages do not alter timing or metadata beyond their specific export role.
