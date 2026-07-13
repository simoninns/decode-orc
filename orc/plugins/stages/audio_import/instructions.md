# Audio Import

Attaches an external WAV file to the pipeline as a new audio channel pair. Use it to bring separately captured or restored audio — a cleaned-up soundtrack, a commentary recording, an externally decoded recording — into the project so sinks can select, embed, or export it alongside the decoder-provided audio.

## When to use

Use Audio Import whenever audio that did not come from the video capture should travel with the video through the pipeline. The imported channel pair is appended after all existing pairs (existing pair numbers are unchanged) and reports origin `IMPORTED`. If the recording needs nudging into sync with the picture, follow with an Audio Align stage targeting the new channel pair.

## What it does

The stage wraps the incoming frame representation and appends one audio channel pair served from the WAV file. Video, dropout hints, EFM/AC3 signal data, and all existing audio channel pairs pass through untouched.

The WAV must be RIFF/WAVE, PCM, stereo, 16- or 24-bit, at any sample rate. Anything else fails validation with a reason.

On import the material is converted to the pipeline's only audio form — 48 kHz synchronous (frame-locked) 24-bit stereo, per SMPTE 272M:

- 16-bit samples are widened to 24-bit (exactly reversible); 24-bit samples are used as-is.
- The stream is resampled from the WAV's header rate to exactly 48000 Hz (high-quality SoXR conversion; a 48000 Hz input passes through unchanged).
- The result is segmented into per-frame blocks following the audio frame sequence (PAL: 1920 stereo pairs per frame; NTSC/PAL-M: 1602/1601 pairs over a 5-frame sequence). Material shorter than the project is padded with silence; excess is truncated.

Appending the channel pair counts toward the pipeline's limit of 8 channel pairs; the stage fails validation if the input already carries 8 pairs.

## Parameters

### wav_path
File path, required. The WAV file to attach.

### pair_name
String, default empty. Human-readable name for the channel pair (shown by sinks, e.g. as the stream title in Video Sink). Empty uses the WAV file name.

## Tools

This stage has no interactive tools.

## Status Indicator

The coloured dot in the top-right corner of the node shows its configuration status.

| Colour | Meaning |
|--------|---------|
| Green | Fully configured and ready to run. All required parameters are set. |
| Yellow | Partially configured. The stage can run but will use default or reduced behaviour — for example, pass-through mode or console-only output. Review the parameters for optional settings. |
| Red | Not configured. One or more required parameters are missing and the stage cannot run. |

Parameters can be set via **Edit Parameters...** in the node context menu. Some stages also provide interactive stage tools (listed under **Tools** above) that set parameters directly from within the tool.
