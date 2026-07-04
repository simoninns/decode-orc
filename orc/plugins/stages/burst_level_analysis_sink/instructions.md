# Burst Level Analysis Sink

Measures the colour burst amplitude for each field in the incoming video stream and displays the results in a chart. Use this sink to assess colour burst stability across a capture or to compare burst levels between multiple sources.

## When to use

Connect this stage when you want to compare colour burst stability between two captures of the same disc, or diagnose why colours look unstable in a particular region. Connect one instance per source in parallel and trigger each. The chart shows burst amplitude over time, making it easy to spot regions of instability or compare sources side by side.

## What it does

Reads each field from the incoming stream and measures the colour burst amplitude. Computes per-field measurements and aggregate statistics (mean, variance, minimum, maximum). After triggering, the Burst Level Analysis tool is automatically invoked to display the results in a chart window. The dataset is cached in the stage instance and can be retrieved from the Stage Tools menu at any time after the trigger completes.

## Parameters

### output_path (file path)
Destination CSV file for the burst metrics. Leave empty to skip file output.

### write_csv (bool)
Enable writing the results to CSV at trigger time. Default: `false`.

## Tools

### Burst Level Analysis
Displays per-frame colour-burst amplitude measurements in a chart window. This tool is automatically invoked after the stage is triggered. It can also be opened manually at any time from the Stage Tools menu once results are available.

## Notes

- The chart updates only after the trigger completes; it does not display live results during processing.
- Connect one instance before and one after the Stacker stage to compare burst stability across captures.
- This stage does not modify the video stream; it is a pure sink.

## Status Indicator

The coloured dot in the top-right corner of the node shows its configuration status.

| Colour | Meaning |
|--------|---------|
| Green | Fully configured and ready to run. All required parameters are set. |
| Yellow | Partially configured. The stage can run but will use default or reduced behaviour — for example, pass-through mode or console-only output. Review the parameters for optional settings. |
| Red | Not configured. One or more required parameters are missing and the stage cannot run. |

Parameters can be set via **Edit Parameters...** in the node context menu. Some stages also provide interactive stage tools (listed under **Tools** above) that set parameters directly from within the tool.
