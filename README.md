# Decode Orc

**decode-orc** is a cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows.

![](./assets/decode-orc-icon-small.png)

It aims to brings structure and consistency to complex LaserDisc decoding processes, making them easier to run, repeat, and understand.

`decode-orc` is a direct replacement for the existing ld-decode-tools, coordinating each step of the process and keeping track of inputs, outputs, and results.

The project aims to:
- Make advanced LaserDisc and tape workflows (from TBC to chroma) easier to manage
- Reduce manual steps and error-prone command sequences
- Help users reproduce the same results over time

A graphical interface is also planned, alongside support for automated and scripted workflows.

This project is under active development.

# Vision

## 1. Overview

The system is designed as an **orchestrated processing environment**, not a linear tool chain. Processing is described declaratively using a **Directed Acyclic Graph (DAG)** stored in a project file. The DAG connects independent processing nodes that operate on video data and associated metadata.

All business logic lives in a shared **core library** (orc-core). User interfaces (GUI (orc-gui) or CLI (orc-cli)) are thin shells that load a project file and execute the same processing graph in exactly the same way.

---

## 2. Sources and Video Formats

### 2.1 Sources

A **source** represents incoming decoded data. Examples include:

* LaserDisc captures
* Tape-based sources (e.g. VHS)

Sources define *where the data comes from*, not how it is processed.

### 2.2 Video Formats

Each source has an underlying **video format**, such as:

* PAL
* NTSC

The key architectural assumption is that:

* The *video format and framing* remain consistent across sources of the same format
* Differences between source types are primarily expressed in **metadata**

Examples:

* CLV vs CAV LaserDiscs differ mainly in timecode metadata, not video structure
* VHS may provide split luma/chroma TBC files, but the underlying format (PAL/NTSC) is unchanged

This separation allows new source types to be added without redefining the entire processing pipeline.

---

## 3. Processing Nodes and the DAG

### 3.1 Node Types

Processing is performed by **nodes** connected in a DAG. Nodes fall into structural categories:

* **One-to-one**: simple transforms
* **One-to-many**: splitters
* **Many-to-one**: mergers
* **Many-to-many**: complex transforms

The DAG explicitly defines how nodes connect and how data flows.

### 3.2 Signal Transformation Nodes

Signal transformation nodes perform all actual processing, such as:

* Dropout correction
* Stacking
* Mapping
* Filtering

Nodes are **independent and re-orderable**. There is no implicit state passed along a linear chain.

---

## 4. Sinks (Outputs)

Every DAG must ultimately terminate in one or more **sinks**.

Typical sink types:

* **Decoder-format sink**: outputs data in the same decoded format as the source
* **Conversion sink**: outputs a different representation (e.g. RGB frames, chroma-decoded video)

A sink consumes the final output of the DAG and produces artifacts for downstream use.

---

## 5. Observers and Metadata Extraction

### 5.1 Observers

Nodes can have associated **observers**. Observers do not modify data; they *inspect* it.

Observers:

* Monitor node outputs
* Extract metadata from the video stream
* Are typically source-type specific

### 5.2 Example: VBI

A VBI observer:

* Watches video fields leaving a node
* Extracts VBI information
* Produces structured VBI metadata

### 5.3 Rationale

Some transformations improve video quality and metadata accuracy (e.g. stacking, correction). By placing observers at node outputs:

* Observers always see the **best available version** of the signal
* Metadata improves as processing progresses

---

## 6. Field-Based Navigation Model

### 6.1 Fields as the Atomic Unit

The smallest navigable unit in the system is a **field**.

* Each source field is assigned a unique sequential **Field ID**
* All processing operates on one field at a time

### 6.2 Virtual Field IDs

Certain nodes can **remap Field IDs** into a virtual sequence.

Example:

* Disc mapping rewrites field order to correct skips or jumps
* Output fields no longer correspond 1:1 with source field numbers

The result is a corrected, logical field timeline.

---

## 7. Multi-Source Alignment and Stacking

Virtual field mapping enables advanced multi-source workflows:

* Each source can be independently mapped
* Multiple mapped sources can feed a many-to-one node

Example:

* Disc stacking combines multiple captures of the same master
* Each source is aligned via mapping before stacking

This allows stacking even when sources differ due to skips, capture errors, or timing differences.

---

## 8. Interactive Preview and Navigation

Because each node operates on individual fields:

* Any node in the DAG can be selected
* The system can produce the *current processed version* of a chosen field
* All associated observed metadata is available

This enables:

* Live previews at arbitrary DAG points
* Immediate feedback when the graph is modified

---

## 9. Core Library vs GUI/CLI

### 9.1 Core Responsibilities

The **core library** contains:

* All processing logic
* All data models
* DAG execution
* Metadata handling

### 9.2 GUI and CLI

The GUI and CLI:

* Load and save project files
* Construct or execute DAGs
* Contain no processing logic

A project file created in the GUI can be executed *unchanged* by the CLI.

---

## 10. Project Files and Reproducibility

Project files describe:

* Sources
* DAG structure
* Node configuration

They do **not** encode processing results.

Benefits:

* Exact reproduction of workflows
* Improved processing automatically applies when node implementations improve
* Historical projects can be rerun without modification

This removes the need for scripting and ensures long-term reproducibility.

---

## 11. Structural Transforms (Cut, Slice, Combine)

Beyond signal correction, nodes can perform structural operations:

* **Cut**: select specific field/frame ranges
* **Slice**: extract subsets of captures
* **Combine**: join multiple captures into a single logical source

Examples:

* Feeding only overlapping frame ranges into a stacking node
* Joining multiple interrupted captures into one continuous source

These transforms simplify downstream processing and alignment.

---

## 12. Orchestration vs Legacy Tool Chains

Unlike earlier ld-decode tools:

* This is **not a linear chain**
* Metadata and video are not repeatedly rewritten in-place

Instead:

* Processing is orchestrated by the DAG
* Nodes are independent and re-arrangeable
* The project file is the single source of truth

This makes the system more flexible, maintainable, and repeatable.

---

## 13. Multi-Project Integration and CI/CD

The orchestrator is designed to be reused across multiple decode projects.

### 13.1 Submodule Strategy

* The orchestrator is included as a **submodule** in upstream projects
* Upstream CI pipelines feed decoded data into the orchestrator DAGs

### 13.2 Continuous Validation

* CI verifies that upstream changes do not break downstream decoding
* Failures alert contributors during the PR process

Outcomes:

* Clear ownership of breaking changes
* Early detection of integration issues
* Strong guarantees of long-term stability

---

## 14. Summary

This architecture provides:

* Field-accurate navigation
* Flexible multi-source orchestration
* Reproducible processing
* GUI and CLI parity
* Long-term maintainability

It forms a robust foundation for current and future LaserDisc and tape decode workflows.

---

## 15. Building and Usage

### 15.1 Dependencies

**System Requirements (must be installed):**
- **CMake** >= 3.20
- **C++17** compatible compiler (GCC 8+, Clang 7+, MSVC 2019+)
- **spdlog**: C++ logging library (header-only, but installed via package manager)
- **SQLite3**: Database for TBC metadata
- **yaml-cpp**: YAML parsing library
- **Qt6** (optional, for GUI): Widgets module

**Installation commands:**

Ubuntu/Debian:
```bash
sudo apt install cmake build-essential libspdlog-dev libsqlite3-dev libyaml-cpp-dev qt6-base-dev
```

Fedora/RHEL:
```bash
sudo dnf install cmake gcc-c++ spdlog-devel sqlite-devel yaml-cpp-devel qt6-qtbase-devel
```

macOS (via Homebrew):
```bash
brew install cmake spdlog sqlite yaml-cpp qt@6
```

### 15.3 Building

```bash
mkdir build
cd build
cmake ../orc
make -j4
```

This builds:
- `orc-core`: Core processing library
- `orc-cli`: Command-line interface for batch processing
- `orc-gui`: Qt6-based graphical interface (if Qt6 is installed)
- Test executables

**Note**: Version numbers are automatically generated from the git commit hash. If there are uncommitted changes, the version will include a `-dirty` suffix.

### 15.4 Running orc-cli

```bash
./bin/orc-cli --dag pipeline.yaml input.tbc output.tbc
```

Optional flags:
- `--version` or `-v`: Show version information (git commit hash)
- `--log-level <level>`: Set logging verbosity (trace, debug, info, warn, error, critical, off). Default: info

Examples:
```bash
# Show version
./bin/orc-cli --version

# Run with debug logging
./bin/orc-cli --log-level debug --dag pipeline.yaml input.tbc output.tbc
```

### 15.5 Running orc-gui

```bash
./bin/orc-gui [project-file.orc-project]
```

Optional flags:
- `--version` or `-v`: Show version information (git commit hash)
- `--log-level <level>`: Set logging verbosity (trace, debug, info, warn, error, critical, off). Default: info

Examples:
```bash
# Show version
./bin/orc-gui --version

# Run with trace logging
./bin/orc-gui --log-level trace
```

### 15.6 Logging

Both orc-cli and orc-gui use **spdlog** for structured logging with color-coded console output:

- **trace**: Very detailed execution flow (typically for debugging library internals)
- **debug**: Detailed execution flow (DAG operations, field rendering, etc.)
- **info**: High-level operations (project loading, source addition, etc.) - **default**
- **warn**: Potential issues that don't prevent operation
- **error**: Errors that prevent specific operations
- **critical**: Fatal errors
- **off**: Disable all logging

Qt messages (qDebug, qInfo, qWarning, etc.) in orc-gui are automatically bridged to spdlog and prefixed with `[Qt]`.