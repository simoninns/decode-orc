# Sink (3rd Party) Stages

Sink 3rd party stages are the **endpoints of a decode-orc pipeline** targeting external tools (external from the ld-decode and vhs-decode projects). They consume processed data from upstream stages and write results to disk or hardware. Unlike transform stages, sink stages do not produce outputs that can be connected further downstream.

A pipeline may contain **multiple sink stages** in parallel, allowing the same processed stream to be written in different formats or to different destinations.

Sink core stages are used to:

* Write final video outputs (TBC + metadata)
* Export auxiliary data such as audio, EFM, or closed captions
* Output video directly to hardware for monitoring or capture
* Export intermediate data for inspection or external tools

---

## HackDAC Sink

| | |
|-|-|
| **Stage id** | `hackdac_sink` |
| **Stage name** | HackDAC Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Output video directly to HackDAC hardware |

**Use this stage when:**

* You want real-time analogue video output
* Monitoring pipeline output on CRT or analogue equipment
* Testing signal characteristics on real hardware

**What it does**

This stage streams processed video fields directly to connected HackDAC hardware.

**Parameters**

* Hardware-specific parameters may be supported depending on build and platform.
* Typical configurations are provided externally rather than via stage parameters.

**Notes**

* This stage is hardware-dependent.
* Timing and field order must already be correct upstream.

---

## Daphne VBI Sink

TBA
