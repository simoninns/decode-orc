# SNR Analysis Sink

Estimates the signal-to-noise ratio of the incoming video stream and displays white SNR and black SNR measurements per field. Use this sink to quantify noise improvement from stacking or to compare the SNR of different capture setups.

## When to use

Connect this stage after each source or after the Stacker stage when you want to measure the noise level in the signal. Comparing SNR before and after stacking shows the noise reduction achieved. Comparing SNR between two capture setups identifies which has better signal quality. Higher SNR means less noise and a better decode quality.

## What it does

Estimates signal-to-noise ratio using spatial and temporal analysis of the incoming video stream. Reports white SNR and black SNR per field. Computes per-field measurements and aggregate statistics. After triggering, the SNR Analysis tool is automatically invoked to display the results in a chart window. Results are consistent across comparable pipelines, allowing meaningful cross-capture comparison. The dataset is cached and can be retrieved from the Stage Tools menu after the trigger completes.

## Parameters

### output_path (file path)
Destination CSV file for the SNR metrics. Leave empty to skip file output.

### write_csv (bool)
Enable writing the results to CSV at trigger time. Default: `false`.

## Tools

### SNR Analysis
Displays white SNR and black SNR metrics over time in a chart window. This tool is automatically invoked after the stage is triggered. It can also be opened manually from the Stage Tools menu once results are available.

## Notes

- SNR results are comparable across pipelines using the same measurement methodology, making cross-capture comparison meaningful.
- Connect one instance after each source and one after the Stacker to see the SNR improvement from stacking.
- This stage does not modify the video stream; it is a pure sink.

## Status Indicator

The coloured dot in the top-right corner of the node shows its configuration status.

| Colour | Meaning |
|--------|---------|
| Green | Fully configured and ready to run. All required parameters are set. |
| Yellow | Partially configured. The stage can run but will use default or reduced behaviour — for example, pass-through mode or console-only output. Review the parameters for optional settings. |
| Red | Not configured. One or more required parameters are missing and the stage cannot run. |

Parameters can be set via **Edit Parameters...** in the node context menu. Some stages also provide interactive stage tools (listed under **Tools** above) that set parameters directly from within the tool.
