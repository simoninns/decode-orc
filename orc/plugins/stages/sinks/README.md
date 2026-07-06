## Video Sink Stage - Shared Implementation

This directory contains decode-orc's video sink stage and its shared implementation.

### Architecture

- **`common/`** — Video sink implementation and utilities
  - `video_sink_stage.h/cpp` — The Video Sink stage: chroma decode engine plus raw/FFmpeg output
  - `output_backend.h/cpp` — Base output backend interface
  - `ffmpeg_output_backend.h/cpp` — FFmpeg output implementation
  - `raw_output_backend.h/cpp` — Raw binary output implementation
  - `video_parameter_safety.h` — Video parameter validation utilities
  - `decoders/` — Chroma decoder implementations

- **`video_sink/`** — Video sink plugin (thin wrapper)
  - Loads `VideoSinkStage` from `common/`
  - Registers the stage with the plugin system

### Output Modes

The single **video_sink** stage selects its output path via the `output_mode` parameter:

- **ffmpeg** — Encoded output to FFmpeg-supported containers (MP4, MKV, MOV, MXF) with optional embedded audio, closed captions, and chapter metadata
- **raw** — Uncompressed raw binary output (RGB48, YUV444P16, Y4M)

The legacy `raw_video_sink` and `ffmpeg_video_sink` stages were merged into this stage; projects referencing the old stage names are migrated on load.

### Plugin SDK Boundary

All stage implementations use only the public SDK interfaces (`VideoFieldRepresentation`, `DAGStage`, etc.). No private host headers are exposed in public APIs or headers.
