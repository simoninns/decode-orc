# LD Sink

Writes the processed pipeline result back to the ld-decode / vhs-decode TBC file format, producing a `.tbc` video file and a `.tbc.db` metadata database. The output is fully compatible with the ld-tools ecosystem.

## When to use

Use this stage as the final output stage when you want to feed the processed result into existing ld-decode tools such as ld-chroma-decoder, ld-analyse, or ld-process-vbi. This is the most common archival output stage for LaserDisc and tape workflows — run stacking, dropout correction, and any other transforms upstream, then connect LD Sink at the end of the pipeline.

## What it does

For every field that passes through the pipeline, LD Sink writes the raw field samples to a `.tbc` binary file and records the associated metadata (field number, parity, VBI observations, hints, and all per-field observations) to a `.tbc.db` SQLite database. The stage also supports pipeline preview — you can inspect what will be written before triggering.

## Parameters

### output_path (string)
Base path for the output files. Required. The stage appends the extensions automatically: `.tbc` for video fields (a trailing `.tbc` in the parameter is kept as-is) and `.tbc.db` for the metadata database.

## Notes

The output path must be writable. If the target directory does not exist the stage will fail at trigger time. This stage writes video and metadata only — export analogue audio, EFM, or AC3 RF data with the Analogue Audio Sink, Raw EFM Data Sink / EFM Decoder Sink, or AC3 RF Sink stages connected in parallel.

## Status Indicator

The coloured dot in the top-right corner of the node shows its configuration status.

| Colour | Meaning |
|--------|---------|
| Green | Fully configured and ready to run. All required parameters are set. |
| Yellow | Partially configured. The stage can run but will use default or reduced behaviour — for example, pass-through mode or console-only output. Review the parameters for optional settings. |
| Red | Not configured. One or more required parameters are missing and the stage cannot run. |

Parameters can be set via **Edit Parameters...** in the node context menu. Some stages also provide interactive stage tools (listed under **Tools** above) that set parameters directly from within the tool.
