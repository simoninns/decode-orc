# EFM Decoder Sink

Accumulates EFM t-values from the pipeline and decodes them through a full EFM decode pipeline, producing either a PCM WAV audio file or ECMA-130 binary sector data. This is the primary stage for extracting digital audio or data content from LaserDiscs.

## When to use

Use the audio mode when extracting CD-quality digital audio from a LaserDisc. Enable `audacity_labels` to get chapter markers in Audacity, and `no_timecodes` for early CAV discs that pre-date the EFM timecode specification. Use the data mode when extracting ECMA-130 data sectors from a CD-Video or data LaserDisc; enable `output_metadata` to capture a record of missing or corrupt sectors for quality assessment.

## What it does

The stage collects EFM t-values field by field from the upstream VideoFieldRepresentation and feeds them through the EfmProcessor decode pipeline, which performs demodulation, error detection, error correction (CIRC), and de-interleaving. In audio mode the result is written as 44.1 kHz 16-bit stereo PCM in a RIFF WAV container. In data mode the result is written as raw ECMA-130 binary sectors. Optional sidecar files (Audacity labels, bad-sector maps, decode reports) are written alongside the main output when their respective flags are enabled.

## Parameters

### output_path (string)
Path to the output file. Use a `.wav` extension for audio mode and `.bin` for data mode. Required.

### decode_mode (string)
Decode target. Values: `audio` (PCM WAV output) or `data` (ECMA-130 sector output). Default: `audio`.

### no_timecodes (boolean)
Disable timecode verification during decode. Needed for early CAV discs that pre-date the EFM timecode specification. Default: `false`.

### audacity_labels (boolean)
Write an Audacity label file alongside the audio output, marking chapter boundaries and positions of missing samples. Audio mode only. Default: `false`.

### no_audio_concealment (boolean)
Disable interpolation-based error concealment for uncorrectable audio samples. When disabled, affected samples are zeroed instead of interpolated. Audio mode only. Default: `false`.

### ignore_preemphasis (boolean)
Ignore the 50/15 µs pre-emphasis CONTROL flag (IEC 60908 §17.5) and write the audio exactly as decoded. When unchecked (default), sections flagged as pre-emphasised are de-emphasised during decode with a 50/15 µs filter, so the output plays back with a flat response. Enable this only if you want the raw pre-emphasised samples (e.g. to apply de-emphasis yourself downstream). When Audacity labels are enabled, the label for a pre-emphasised track is annotated `Preemphasis:50/15us(removed)` when de-emphasis was applied, or `Preemphasis:50/15us` when this flag is set. Audio mode only. Default: `false`.

### zero_pad (boolean)
Zero-pad the start of the audio output so that playback begins at 00:00:00.0 relative to the first valid EFM timecode. Audio mode only. Default: `false`.

### no_wav_header (boolean)
Output raw PCM samples without a RIFF WAV header. Useful when the output will be piped into another tool that expects headerless PCM. Audio mode only. Default: `false`.

### output_metadata (boolean)
Write a bad-sector map file alongside the sector output recording the position of any missing or corrupt sectors. Data mode only. Default: `false`.

### report (boolean)
Write a detailed decode statistics report file alongside the main output. Default: `false`.

## Notes

The upstream source stage must supply EFM data — the pipeline will abort if no EFM data is present in the VideoFieldRepresentation. Audio and data modes are mutually exclusive; set `decode_mode` before triggering. Parameters marked "audio only" or "data only" are silently ignored when the corresponding mode is not active.

## Status Indicator

The coloured dot in the top-right corner of the node shows its configuration status.

| Colour | Meaning |
|--------|---------|
| Green | Fully configured and ready to run. All required parameters are set. |
| Yellow | Partially configured. The stage can run but will use default or reduced behaviour — for example, pass-through mode or console-only output. Review the parameters for optional settings. |
| Red | Not configured. One or more required parameters are missing and the stage cannot run. |

Parameters can be set via **Edit Parameters...** in the node context menu. Some stages also provide interactive stage tools (listed under **Tools** above) that set parameters directly from within the tool.
