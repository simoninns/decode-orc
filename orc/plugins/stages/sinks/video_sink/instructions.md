# Video Sink

Decodes the processed video stream to colour video and writes it to a file. The Output Mode parameter selects between FFmpeg-encoded output (MP4/MKV/MOV/MXF containers with optional audio, closed captions, and chapter metadata) and uncompressed raw output (RGB48, YUV444P16, Y4M).

## When to use

Use this sink at the end of your pipeline to export the decoded video.

- **FFmpeg mode** produces a playable, distributable, or archival video file. Choose `mp4-h264` for wide device compatibility or `mkv-ffv1` for lossless preservation. Use the FFmpeg Preset Config tool to quickly apply well-tested encoder combinations.
- **Raw mode** produces an uncompressed output for integration with external tools such as FFmpeg, VirtualDub, or image-processing scripts. Choose `y4m` for maximum compatibility with tools that understand Y4M headers, or `rgb`/`yuv` for direct integration with image processing pipelines.

## What it does

Applies the selected chroma decoder to convert the incoming TBC video stream to colour video, then writes the result according to the selected output mode: encoded via FFmpeg into the chosen container and codec, or as raw decoded frames without compression. In FFmpeg mode it can optionally embed analogue audio, closed captions (as mov_text subtitles, MP4 only), and chapter markers derived from VBI data.

## Parameters

### output_path (string)
Output file path. Match the extension to the selected mode and format: `.mp4`, `.mkv`, `.mov`, or `.mxf` for FFmpeg output; `.rgb`, `.yuv`, or `.y4m` for raw output. Required.

### decoder_type (string)
Chroma decoder to apply. PAL: `pal2d`, `transform2d`, `transform3d`. NTSC: `ntsc1d`, `ntsc2d`, `ntsc3d`, `ntsc3dnoadapt`. Other: `mono`.

### output_mode (string)
Output path selection. Values: `ffmpeg` (encoded output via FFmpeg) or `raw` (uncompressed file output). Default: `ffmpeg`.

### raw_format (string)
Raw output format (raw mode only). Values: `rgb` (RGB48, 16-bit per channel), `yuv` (YUV444P16, planar), `y4m` (YUV444P16 with Y4M header). Default: `rgb`.

### ffmpeg_format (string)
Container and codec (FFmpeg mode only). Values include `mp4-h264`, `mkv-ffv1`, `mov-prores`, `mov-v210`, `mov-v410`, `mxf-mpeg2video`, `mov-h264`, `mp4-hevc`, `mov-hevc`, and `mp4-av1`. Default: `mp4-h264`.

### chroma_gain (double)
Chroma gain multiplier applied before output. Range: 0.0–10.0. Default: `1.0`.

### chroma_phase (double)
Chroma phase rotation in degrees. Range: -180 to 180. Default: `0`.

### luma_nr (double)
Luma noise reduction level. Higher values reduce luminance noise at the cost of sharpness.

### chroma_nr (double)
Chroma noise reduction level. Higher values reduce colour noise at the cost of chroma resolution.

### ntsc_phase_comp (bool)
Enable NTSC phase compensation. Applies to NTSC sources only.

### simple_pal (bool)
Enable simple PAL mode (1D UV filter for Transform PAL: simpler, faster, lower quality). Applies to the `transform2d`/`transform3d` decoders only.

### transform_threshold (double)
Similarity threshold for the Transform PAL decoder. Higher values apply more transform filtering. Range: 0.0–1.0. Default: `0.4`. Applies to the `transform2d`/`transform3d` decoders only.

### chroma_weight (double)
Chroma weight for the NTSC 3D adaptive filter. Higher values prefer more of the 2D result. Range: 0.0–10.0. Default: `1.0`. Applies to the `ntsc3d`/`ntsc3dnoadapt` decoders only.

### adapt_threshold (double)
NTSC 3D adaptive filter threshold. Higher values prefer more of the 3D result. Range: 0.0–10.0. Default: `1.0`. Applies to the `ntsc3d` decoder only.

### output_padding (int)
Alignment padding added to each output frame. Default: `8`.

### encoder_preset (string)
FFmpeg mode only. Encoder speed/quality trade-off. Values: `fast`, `medium`, `slow`, `veryslow`. Slower presets produce smaller files at the same quality level.

### encoder_crf (int)
FFmpeg mode only. Constant Rate Factor for quality-based encoding. Range: 0–51; lower values produce higher quality and larger files. Default: `18`. Used when `encoder_bitrate` is `0`.

### encoder_bitrate (int)
FFmpeg mode only. Target bitrate in bits per second. When non-zero, overrides CRF mode. Default: `0` (use CRF).

### hardware_encoder (string)
FFmpeg mode only. Hardware-accelerated encoding backend. Values: `none`, `vaapi`, `nvenc`, `qsv`, `amf`, `videotoolbox`. Default: `none`.

### prores_profile (string)
FFmpeg mode only, `mov-prores` format. ProRes quality profile: `proxy`, `lt`, `standard`, `hq`, `4444`, `4444xq`. Default: `hq`.

### use_lossless_mode (bool)
FFmpeg mode only. Enable mathematically lossless encoding (H.264/H.265/AV1 only, overrides CRF). Default: `false`.

### apply_deinterlace (bool)
FFmpeg mode only. Apply bwdif deinterlacing filter for progressive web playback. Default: `false`.

### embed_audio (bool)
FFmpeg mode only. Embed analogue audio from the source into the output file. Requires audio data to be present in the pipeline. Default: `false`.

### embed_closed_captions (bool)
FFmpeg mode only. Embed closed captions as mov_text subtitles. MP4/MOV output only; converts EIA-608 captions from VBI. Default: `false`.

### embed_chapter_metadata (bool)
FFmpeg mode only. Write chapter markers derived from VBI data into the output file. Default: `false`.

## Tools

### FFmpeg Preset Config
Opens a preset helper dialog that lets you select common encoder configurations without manually setting each parameter. Invoke from the Stage Tools menu. Applying a preset switches the stage to FFmpeg output mode.

## Notes

- Raw mode does not support audio, closed caption, or chapter embedding; those options apply to FFmpeg output only.
- Raw output files can be very large; ensure sufficient disk space before triggering.
- The `y4m` raw format adds a Y4M header to the file, making it directly readable by tools such as FFmpeg and rav1e without specifying the pixel format manually.
- Closed caption embedding is only supported in MP4/MOV containers.
- CRF and bitrate modes are mutually exclusive; set `encoder_bitrate` to a non-zero value to switch from CRF mode.

## Status Indicator

The coloured dot in the top-right corner of the node shows its configuration status.

| Colour | Meaning |
|--------|---------|
| Green | Fully configured and ready to run. All required parameters are set. |
| Yellow | Partially configured. Set the output path before triggering. |

Parameters can be set via **Edit Parameters...** in the node context menu. The FFmpeg Preset Config tool (listed under **Tools** above) sets encoder parameters directly from within the tool.
