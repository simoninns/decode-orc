# Analogue Audio Sink

Extracts one audio track from the processed video pipeline and writes it to a standard WAV file, synchronised to the processed video timeline. Use this stage when you want the audio independently from the full TBC output.

## When to use

Add this sink in parallel with the LD Sink stage when your LaserDisc or tape source carries audio tracks and you need the audio as a standalone file — for example to process it in a DAW, verify audio/video sync, or archive it separately. Because it is a parallel sink, adding it does not affect the video output.

## What it does

The pipeline can carry up to 16 stereo audio tracks (analogue capture audio, decoded EFM digital audio, imported WAV files, or tracks derived by transforms). The `track` parameter selects which one this sink writes. The stage reads the selected track's 16-bit signed little-endian stereo PCM samples, wraps them in a standard RIFF WAV container, and writes the result to the specified path.

Each track is either **frame-locked** or **free-running**:

- Frame-locked tracks carry exact integer stereo pairs per video frame and follow the processed frame sequence, so any frame trimming or reordering performed upstream is reflected in the output. For PAL (25 fps) the locked rate is exactly 44,100 Hz. For NTSC and PAL-M (30000/1001 fps) the locked rate is 44100000/1001 Hz ≈ 44,055.94 Hz (exactly 1470 stereo pairs per frame), and the `sample_rate_mode` parameter selects how that audio is exported.
- Free-running tracks (for example decoded EFM digital audio) are a 44,100 Hz stream independent of frame timing. They are written verbatim — sample values are never resampled — with the WAV header declaring 44,100 Hz.

## Parameters

### output_path (string)
Path to the output WAV file. Required. The file will be created or overwritten at trigger time.

### track (integer)
Pipeline audio track to write. Track indices are 0-based, matching the CVBS container's `_audio_NN.wav` numbering. Default 0. Triggering fails if the selected track does not exist in the input.

### sample_rate_mode (string, NTSC/PAL-M projects only)
Selects the output sample rate for frame-locked NTSC/PAL-M audio:

- `locked_44056` (default) — writes the frame-locked samples unmodified; the WAV header declares the NTSC frame-rate-matched rate of 44,056 Hz (nearest integer to 44100000/1001 Hz). No resampling is performed, so the samples remain bit-identical to the pipeline audio.
- `free_running_44100` — resamples the audio (SoXR, HQ) to standard free-running 44,100 Hz for tools that expect the standard rate. Duration and A/V sync are preserved.

The parameter is not shown for PAL projects, whose locked audio is already at 44,100 Hz; both modes would produce identical output. The mode is ignored for free-running tracks, which are always written verbatim at 44,100 Hz.

## Notes

Undecoded digital audio carried as an EFM t-value stream or AC3 RF (Dolby Digital) must be extracted with the EFM Decoder Sink or AC3 RF Sink stages respectively — or, for EFM, decoded into a pipeline track with the EFM Audio Decode transform and then written by this sink. Audio stacking (when processing multiple source captures) must be performed upstream, for example via the Stacker stage's `audio_stacking` parameter.

## Status Indicator

The coloured dot in the top-right corner of the node shows its configuration status.

| Colour | Meaning |
|--------|---------|
| Green | Fully configured and ready to run. All required parameters are set. |
| Yellow | Partially configured. The stage can run but will use default or reduced behaviour — for example, pass-through mode or console-only output. Review the parameters for optional settings. |
| Red | Not configured. One or more required parameters are missing and the stage cannot run. |

Parameters can be set via **Edit Parameters...** in the node context menu. Some stages also provide interactive stage tools (listed under **Tools** above) that set parameters directly from within the tool.
