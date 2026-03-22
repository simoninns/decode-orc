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

For instructions on how to install please see the documentation for your preferred platform:

- [Linux Flatpak installation](https://simoninns.github.io/decode-orc-docs/decode-orc/installation/linux-flatpak/)
- [MacOS DMG installation](https://simoninns.github.io/decode-orc-docs/decode-orc/installation/macos-dmg/)
- [Windows MSI installation](https://simoninns.github.io/decode-orc-docs/decode-orc/installation/windows-msi/)

## Building from Source

For comprehensive build instructions covering Nix, CMake, and platform-specific setup, please see [BUILD.md](BUILD.md).

**Quick reference:** Use `nix develop` for reproducible builds with all dependencies pre-configured, or follow the CMake/vcpkg instructions for system package manager setups.

# Using Agentic Coding AI

This repository includes repository-wide custom instructions for agentic coding AI tools. These instructions are **essential** for AI agents to understand the project architecture, build process, testing strategy, and CI/CD requirements.

## GitHub Copilot

If you are using **GitHub Copilot** (coding agent or code review), the custom instructions in [`.github/copilot-instructions.md`](.github/copilot-instructions.md) will be automatically available and used by Copilot.

Key guidance includes:
- **MVP Architecture:** Core, presenters, view-types, and UI layers must remain decoupled. Run `ctest -R MVPArchitectureCheck` before submitting PRs.
- **Build & Test:** Use `nix develop` for reproducible setup, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON` and `ctest --test-dir build --output-on-failure`.
- **Testing Philosophy:** ~80% unit test coverage; mock dependencies; no filesystem/network/clock in unit tests.
- **Multi-OS CI/CD:** Changes must align with Linux (Nix), macOS (DMG packaging), Windows (MSI packaging), and Flatpak workflows.

## Other AI Coding Tools

If you are using a different agentic coding AI tool (e.g., Claude, Gemini, or other coding assistants), **manually provide the instructions from [`.github/copilot-instructions.md`](.github/copilot-instructions.md)** to the AI when asking it to make changes or contributions to this repository. 

This ensures the AI agent understands:
1. The project's strict MVP architecture constraints
2. The correct build commands and dependency setup
3. The unit testing strategy and mocking conventions
4. How to validate changes against the multi-platform CI/CD pipeline
5. Contribution hygiene and PR expectations

Sharing these instructions significantly improves the likelihood that AI-generated pull requests will pass CI/CD checks and meet project standards on the first attempt.

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