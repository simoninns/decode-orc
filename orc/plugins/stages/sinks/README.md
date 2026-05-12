## Video Sink Stages - Shared Implementation

This directory groups decode-orc's video sink stages and their shared implementations.

### Architecture

The video sink stages follow a shared implementation pattern:

- **`common/`** — Shared video sink implementations and utilities
  - `chroma_sink_stage.h/cpp` — Base chroma sink stage implementation
  - `ffmpeg_video_sink_stage.h/cpp` — FFmpeg-backed video file sink
  - `raw_video_sink_stage.h/cpp` — Raw binary video file sink
  - `output_backend.h/cpp` — Base output backend interface
  - `ffmpeg_output_backend.h/cpp` — FFmpeg output implementation
  - `raw_output_backend.h/cpp` — Raw binary output implementation
  - `video_parameter_safety.h` — Video parameter validation utilities
  - `decoders/` — Chroma decoder implementations

- **`ffmpeg_video_sink/`** — FFmpeg video sink plugin (thin wrapper)
  - Loads `FFmpegVideoSinkStage` from `common/`
  - Registers the stage with the plugin system

- **`raw_video_sink/`** — Raw video sink plugin (thin wrapper)
  - Loads `RawVideoSinkStage` from `common/`
  - Registers the stage with the plugin system

### Consumer Stages

Both video sink plugins consume the shared implementations:
- **ffmpeg_video_sink** — Outputs video to FFmpeg-supported formats (MP4, etc.)
- **raw_video_sink** — Outputs raw binary video data

### Plugin SDK Boundary

All stage implementations use only the public SDK interfaces (`VideoFieldRepresentation`, `DAGStage`, etc.). No private host headers are exposed in public APIs or headers.
