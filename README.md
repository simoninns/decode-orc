# Decode Orc

**decode-orc** is a cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows.

![](./assets/decode-orc-logo-small.png)

It aims to brings structure and consistency to complex decoding processes, making them easier to run, repeat, and understand.

`decode-orc` is a direct replacement for the existing ld-decode-tools, coordinating each step of the process and keeping track of inputs, outputs, and results.

The project aims to:
- Make advanced LaserDisc and tape workflows (from TBC to end-user video) easier to manage
- Reduce manual steps and error-prone command sequences
- Help users reproduce the same results over time

Both a graphical interface (orc-gui) and command-line interface (orc-cli) are implemented for orchestrating workflows.  These commands contain minimal business logic and, instead, rely on the same orc-core following a MVP architecture (Model–View–Presenter) wherever possible.

# Documentation

## User documentation

The user documentation is available via [Github Pages](https://simoninns.github.io/decode-orc-docs/index.html)

## Technical documentation

The technical documentation is available via [Doxygen](https://simoninns.github.io/decode-orc/index.html)

# Installation

The Decode Orc project provides ready-to-install packages for Linux (flatpak), Windows (MSI installer) and Mac OS (DMG).

The release packages can be found in the [release section](https://github.com/simoninns/decode-orc/releases) of the Github repository.

For instructions on how to install please see the [release installtion](https://simoninns.github.io/decode-orc-docs/release-installation.html) instructions.

# Credits

Decode Orc was designed and written by Simon Inns.  Decode Orc's development heavily relied on the original GPLv3 ld-decode-tools which contained many contributions from others.

- Simon Inns (2018-2025) - Extensive work across all tools
- Adam Sampson (2019-2023) - Significant contributions to core libraries, chroma decoder and tools
- Chad Page (2014-2018) - Filter implementations and original NTSC comb filter
- Ryan Holtz (2022) - Metadata handling
- Phillip Blucas (2023) - VideoID decoding
- ...and others (see the original ld-decode-tools source)

It should be noted that the original code for the observers is also based heavily on the ld-decode python code-base (written by Chad Page et al).