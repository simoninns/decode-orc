# NN NTSC Chroma Sink — CLI Usage

The **NN NTSC Chroma Sink** (`nn_ntsc_chroma_sink`) exports a neural-network–decoded NTSC chroma video from the command line. It is a trigger-based sink stage: `orc-cli` executes the project DAG and then triggers all sink nodes, including this one.

---

## Basic workflow

1. Create or open a project in `orc-gui` that includes an `nn_ntsc_chroma_sink` node and set its parameters (output path, format, etc.).
2. Save the project as a `.orcprj` file.
3. Run `orc-cli` with `--process`:

```bash
orc-cli my-ntsc-project.orcprj --process
```

The CLI will execute the pipeline, then trigger the NN NTSC Chroma Sink. Decoded frames are written to the path specified in the project's `output_path` parameter.

---

## Output formats

The output format is set in the project file via the `output_format` parameter:

| Format | Description | Typical use |
|--------|-------------|-------------|
| `rgb` | RGB48 — 16-bit per channel, planar | FFmpeg, ImageMagick |
| `yuv` | YUV444P16 — 16-bit planar Y/U/V | FFmpeg, custom tools |
| `y4m` | YUV444P16 with Y4M stream header | mpv, FFmpeg (no flags needed) |

### Converting RGB48 output with FFmpeg

```bash
# Display or pipe directly
ffmpeg -f rawvideo -pixel_format rgb48le \
       -video_size 760x488 \
       -framerate 30000/1001 \
       -i output.rgb \
       -c:v libx264 -crf 18 -pix_fmt yuv420p \
       output.mp4
```

### Playing Y4M output directly

```bash
mpv output.y4m
```

---

## Deterministic (reproducible) output

By default the stage uses up to 4 parallel worker threads. Multi-threaded ONNX tile processing is non-deterministic across runs due to overlap-add ordering. To produce bit-identical output:

Set `deterministic_output` to `true` in the project file before saving, then run as normal:

```bash
orc-cli my-ntsc-project.orcprj --process
```

With `deterministic_output=true` the stage uses a single thread, making the output reproducible for validation and comparison.

---

## Logging

Use `--log-level debug` to see per-frame progress and configuration details:

```bash
orc-cli my-ntsc-project.orcprj --process --log-level debug
```

Use `--log-file` to capture the full output:

```bash
orc-cli my-ntsc-project.orcprj --process --log-level info --log-file encode.log
```

The stage logs its video parameter mapping, thread count, frame range, and final frame count on completion.

---

## Error conditions

| Message | Cause | Resolution |
|---------|-------|------------|
| `NN NTSC Chroma Sink requires an NTSC source` | The pipeline is processing PAL or PAL-M material | Use a standard Chroma Sink for non-NTSC sources |
| `No output path specified` | `output_path` is empty in the project | Set a valid path in the project before saving |
| `No input — run the pipeline first` | The stage was triggered without the DAG having been executed | Ensure the source stage is connected and the project has been processed |
| `Cannot open output file` | Permissions issue or invalid path | Check the directory exists and is writable |

---

## Notes

* The stage is **NTSC-only**. PAL and PAL-M inputs are rejected at trigger time.
* The ONNX model weights are embedded in the binary — no external model file is required.
* For long captures, the full field buffer is held in memory during decoding. Ensure sufficient RAM is available before triggering on large projects.
* For a quality comparison, add both an `nn_ntsc_chroma_sink` node and a standard `chroma_sink` node (ntsc3d decoder) to the same project. Both will be triggered by `--process` and produce independent outputs in a single pass.
