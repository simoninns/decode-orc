# Decode Orc

**Decode-Orc** is a cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows.

![](assets/decode-orc_logotype-1024x286.png)

It aims to brings structure and consistency to complex decoding processes, making them easier to run, repeat, and understand.

> [!IMPORTANT]
> [Click here for the documentation](https://simoninns.github.io/decode-orc-docs/index.html)

`Decode-Orc` is a direct replacement for the existing ld-decode-tools, coordinating each step of the process and keeping track of inputs, outputs, and results.

The project aims to:
- Make advanced LaserDisc and tape workflows (from TBC to end-user video) easier to manage
- Reduce manual steps and error-prone command sequences
- Help users reproduce the same results over time

Both a graphical interface (orc-gui) and command-line interface (orc-cli) are implemented for orchestrating workflows.  These commands contain minimal business logic and, instead, rely on the same orc-core following a MVP architecture (Model–View–Presenter) wherever possible.

# Documentation

## User documentation

The user documentation is available via [Github Pages](https://simoninns.github.io/decode-orc-docs/index.html)

# Installation

The Decode-Orc project provides ready-to-install packages for Mac OS (DMG).

The release packages can be found in the [release section](https://github.com/simoninns/decode-orc/releases) of the Github repository.

For instructions on how to install please see the [release installtion](https://simoninns.github.io/decode-orc-docs/release-installation.html) instructions.

## Building from Source

### Using Nix (Recommended for Reproducible Builds)

The project supports [Nix](https://nixos.org/) for deterministic, reproducible builds:

```bash
# Enter development environment
nix develop

# Build the project
nix build

# Run the application
nix run .#orc-gui
```

See [docs/NIX-BUILD.md](docs/NIX-BUILD.md) for complete Nix build instructions.

### Traditional Build

For traditional CMake builds with vcpkg, please refer to the [build documentation](https://simoninns.github.io/decode-orc-docs/).

# Crash Reporting

If the application crashes unexpectedly, it will automatically create a diagnostic bundle to help identify and fix the issue. This bundle contains:

- System information (OS, CPU, memory)
- Stack backtrace showing where the crash occurred
- Application logs
- Coredump file (when available)

The crash bundle is saved as a ZIP file in:
- **orc-gui**: `~/Documents/decode-orc-crashes/` (or your home directory)
- **orc-cli**: Current working directory

When reporting a crash issue on [GitHub Issues](https://github.com/simoninns/decode-orc/issues), please:
1. Attach the crash bundle ZIP file (or upload to a file sharing service if too large)
2. Describe what you were doing when the crash occurred
3. Include the contents of `crash_info.txt` from the bundle in your issue description

This information is invaluable for diagnosing and fixing crashes quickly.

# Credits

Decode-Orc was designed and written by Simon Inns.  Decode-Orc's development heavily relied on the original GPLv3 ld-decode-tools which contained many contributions from others.

- Simon Inns (2018-2025) - Extensive work across all tools
- Adam Sampson (2019-2023) - Significant contributions to core libraries, chroma decoder and tools
- Chad Page (2014-2018) - Filter implementations and original NTSC comb filter
- Ryan Holtz (2022) - Metadata handling
- Phillip Blucas (2023) - VideoID decoding
- ...and others (see the original ld-decode-tools source)

It should be noted that the original code for the observers is also based heavily on the ld-decode python code-base (written by Chad Page et al).