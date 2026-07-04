# Dropout Analysis Sink

Reads dropout hints from the incoming video stream and generates statistical summaries of dropout frequency, size, and distribution. Use this sink to quantify dropout presence before and after stacking, or to compare the dropout profile between multiple captures.

## When to use

Connect this stage when you want to compare the dropout profile between multiple captures to decide which to use as primary, or to verify that stacking has reduced dropout count and size. Connect one instance before and one after the Stacker stage and compare the resulting charts to see the improvement.

## What it does

Reads dropout hints present in the incoming stream and computes statistical summaries: total dropout count, per-field dropout counts, size distributions, and line/field density metrics. This stage does not perform dropout detection or correction — it only analyses hints that were already placed in the stream by earlier stages. After triggering, the Dropout Analysis tool is automatically invoked to display the results. The dataset is cached and can be retrieved from the Stage Tools menu after the trigger completes.

## Parameters

### output_path (file path)
Destination CSV file for the dropout metrics. Leave empty to skip file output.

### write_csv (bool)
Enable writing the results to CSV at trigger time. Default: `false`.

## Tools

### Dropout Analysis
Displays dropout frequency, size, and distribution charts. This tool is automatically invoked after the stage is triggered. It can also be opened manually from the Stage Tools menu once results are available.

## Notes

- This stage reads existing dropout hints; it does not detect or correct dropouts itself.
- Results are meaningful only if dropout information is present in the upstream pipeline (e.g. from a source stage that provides dropout hints).
- Connect one instance before the Stacker and one after to see the dropout reduction achieved by stacking.
- This stage does not modify the video stream; it is a pure sink.

## Status Indicator

The coloured dot in the top-right corner of the node shows its configuration status.

| Colour | Meaning |
|--------|---------|
| Green | Fully configured and ready to run. All required parameters are set. |
| Yellow | Partially configured. The stage can run but will use default or reduced behaviour — for example, pass-through mode or console-only output. Review the parameters for optional settings. |
| Red | Not configured. One or more required parameters are missing and the stage cannot run. |

Parameters can be set via **Edit Parameters...** in the node context menu. Some stages also provide interactive stage tools (listed under **Tools** above) that set parameters directly from within the tool.
