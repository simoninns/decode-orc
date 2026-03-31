# Introduction

**decode-orc** is a cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows.

![](../assets/decode-orc_logotype-1024x286.png)

It aims to brings structure and consistency to complex decoding processes, making them easier to run, repeat, and understand.

`decode-orc` is a direct replacement for the existing ld-decode-tools, coordinating each step of the process and keeping track of inputs, outputs, and results.

The project aims to:

- Make advanced LaserDisc and tape workflows (from TBC to chroma) easier to manage
- Reduce manual steps and error-prone command sequences
- Help users reproduce the same results over time

Both a graphical interface (orc-gui) and command-line interface (orc-cli) are implemented for orchestrating workflows.  These commands contain minimal business logic and, instead, rely on the same orc-core following a MVP architecture (Model–View–Presenter) wherever possible.

## Installation

Decode-Orc is available as a pre-built installation package for Windows (MSI), MacOS (DMG) and Linux (Flatpak).  For installation instructions please see the installation section of this document.

## Credits

Decode Orc was designed and written by Simon Inns.  Decode Orc's development heavily relied on the original GPLv3 ld-decode-tools which contained many contributions from others.

- Simon Inns (2018-2025) - Extensive work across all tools
- Adam Sampson (2019-2023) - Significant contributions to core libraries, chroma decoder and tools
- Chad Page (2014-2018) - Filter implementations and original NTSC comb filter
- Ryan Holtz (2022) - Metadata handling
- Phillip Blucas (2023) - VideoID decoding
- ...and others (see the original ld-decode-tools source)

It should be noted that the original code for the observers is also based heavily on the ld-decode python code-base (written by Chad Page et al).
