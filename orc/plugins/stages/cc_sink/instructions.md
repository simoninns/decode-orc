# Closed Caption Sink

Extracts NTSC Line 21 closed caption data from the processed video pipeline and writes it to either Scenarist SCC format or plain text. Use this stage in parallel with the LD Sink stage when archiving captioned NTSC LaserDiscs.

## When to use

Add this sink when you are processing an NTSC LaserDisc that carries Line 21 closed captions and you want to preserve or inspect the caption data. The SCC output can be converted to SRT or VTT for use with modern media players, loaded into caption-authoring tools, or archived alongside the video. The plain text output is useful for quickly reading caption content without specialised tools.

## What it does

For each field in the pipeline, the stage reads the two caption bytes embedded in VBI Line 21 from the VideoFieldRepresentation. It accumulates the byte pairs across the full field sequence and writes them in the chosen format: Scenarist SCC V1.0 (industry-standard, includes HH:MM:SS:FF timestamps and hex byte pairs) or plain text (printable ASCII only, control codes stripped).

## Parameters

### output_path (string)
Path to the closed-caption output file. Required. Conventionally uses a `.scc` extension for SCC format or `.txt` for plain text.

### format (string)
Export format. Values: `Scenarist SCC` (Scenarist SCC V1.0) or `Plain Text`. Default: `Scenarist SCC`.

## Notes

CC data must be present in the source capture — this stage cannot synthesise captions. It handles NTSC Line 21 only; PAL sources do not carry Line 21 CC data. Do not use a Mask Line transform to blank line 21 before this stage, as that will destroy the caption payload. If the source fields contain no CC data the output file will be empty but the stage will not abort.

## Status Indicator

The coloured dot in the top-right corner of the node shows its configuration status.

| Colour | Meaning |
|--------|---------|
| Green | Fully configured and ready to run. All required parameters are set. |
| Yellow | Partially configured. The stage can run but will use default or reduced behaviour — for example, pass-through mode or console-only output. Review the parameters for optional settings. |
| Red | Not configured. One or more required parameters are missing and the stage cannot run. |

Parameters can be set via **Edit Parameters...** in the node context menu. Some stages also provide interactive stage tools (listed under **Tools** above) that set parameters directly from within the tool.
