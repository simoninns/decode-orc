# Sink (3rd Party) Stages

Sink 3rd party stages are the **endpoints of a decode-orc pipeline** targeting external tools (external from the ld-decode and vhs-decode projects). They consume processed data from upstream stages and write results to disk. Unlike transform stages, sink stages do not produce outputs that can be connected further downstream.

A pipeline may contain **multiple sink stages** in parallel, allowing the same processed stream to be written in different formats or to different destinations.

---

## Daphne VBI Sink

| | |
|-|-|
| **Stage id** | `daphne_vbi_sink` |
| **Stage name** | Daphne VBI Sink |
| **Connections** | 1 input → no outputs |
| **Purpose** | Write per-field VBI data in the format required by the Daphne arcade LaserDisc emulator |

**Use this stage when:**

* Archiving a LaserDisc title for use with the Daphne arcade LaserDisc emulator

**What it does**

Reads VBI data from each frame in the incoming stream and writes binary VBI records field by field to a `.vbi` file according to the Daphne VBIInfo specification. The `.vbi` file carries the per-field VBI metadata that Daphne requires to emulate the disc's interactivity correctly.

**Parameters**

* `output_path` (string)
    - Path to the output `.vbi` file.
    - Required.

**Notes**

* This sink produces a file specific to the Daphne emulation project and is not a general-purpose VBI archive format.
* The `.vbi` format is documented at the Daphne VBIInfo wiki page.
* Connect other sinks in parallel if you also need video output.

---

## Removed and relocated stages

### CVBS Sink (now a core sink)

The CVBS Sink stage is categorised as a **Sink (Core)** stage; see the Sink (Core) documentation for details.

### HackDAC Sink (removed in v2.0)

The `hackdac_sink` stage was removed in Decode-Orc 2.0. It is no longer available in the plugin registry. Projects that referenced this stage must be recreated without it.
