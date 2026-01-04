# Decode Orc

**decode-orc** is a cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows.

![](./assets/decode-orc-icon-small.png)

It aims to brings structure and consistency to complex decoding processes, making them easier to run, repeat, and understand.

`decode-orc` is a direct replacement for the existing ld-decode-tools, coordinating each step of the process and keeping track of inputs, outputs, and results.

The project aims to:
- Make advanced LaserDisc and tape workflows (from TBC to chroma) easier to manage
- Reduce manual steps and error-prone command sequences
- Help users reproduce the same results over time

Both a graphical interface (orc-gui) and command-line interface (orc-cli) are implemented for orchestrating workflows.  These commands contain minimal business logic and, instead, rely on the same orc-core following a MVP architecture (Model–View–Presenter) wherever possible.

Decode Orc was designed and written by Simon Inns.  Decode Orc's development heavily relied on the original GPLv3 ld-decode-tools which contained many contributions from others.

- Simon Inns (2018-2025) - Extensive work across all tools
- Adam Sampson (2019-2023) - Significant contributions to core libraries, chroma decoder and tools
- Chad Page (2014-2018) - Filter implementations and original NTSC comb filter
- Ryan Holtz (2022) - Metadata handling
- Phillip Blucas (2023) - VideoID decoding
- ...and others (see the original ld-decode-tools source)

It should be noted that the original code for the observers is also based heavily on the ld-decode python code-base (written by Chad Page et al).

This project is under active development.

# Documentation

## User documentation

TBA

## Technical documentation

The technical documentation is available via [Doxygen](https://simoninns.github.io/decode-orc/index.html)

# Vision

## 1. Overview

The system is designed as an **orchestrated processing environment**, not a linear tool chain. Processing is described declaratively using a **Directed Acyclic Graph (DAG)** stored in a project file. The DAG connects independent processing nodes that operate on video data.

All business logic lives in a shared **core library** (orc-core). User interfaces (orc-gui for the GUI and orc-cli for the CLI) are thin shells that load a project file and execute the same processing graph in exactly the same way.

---

## 2. Sources and Video Formats

### 2.1 Sources

A **source** represents incoming decoded data. Examples include:

* LaserDisc captures
* Tape-based sources (e.g. VHS)

Sources define *where the data comes from* (ld-decode, vhs-decode, etc.), not how it is processed.

### 2.2 Video Formats

Each source has an underlying **video format**, such as:

* PAL/PAL-M
* NTSC

The key architectural assumption is that:

* The *video format and framing* remain consistent across sources of the same format
* Differences between source types are primarily expressed by the associated ingress **metadata**

Examples:

* CLV vs CAV LaserDiscs differ mainly in timecode metadata, not video structure
* VHS may provide split luma/chroma TBC files, but the underlying format (PAL/NTSC) is unchanged

This separation allows new source types to be added without redefining the entire processing pipeline.

---

## 3. Processing Nodes and the DAG

### 3.1 Node Types

Processing is performed by **nodes** connected in a DAG. Nodes fall into structural categories:
* **Output only**: sources (-decode ingress)
* **Input only**: sinks (egress)
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

Nodes are **independent and re-orderable**. There is no implicit state passed along a DAG between nodes.

---

## 4. Observers and Metadata Extraction

### 4.1 Observers

Nodes can have associated **observers**. Observers do not modify data; they *inspect* it.

Observers extract metadata from video signals without modifying them. They observe the signal directly to generate metadata (Note this is *not* ingress metadata from the decoder source).

Note: Certain data like dropout information cannot be observed after-the-fact (since it is generated from data no longer present in the TBC output (such as the original RF sample)). Such data is treated as "hints" through a separate mechanism.

Observers:

* Monitor node outputs
* Extract metadata from the node outputs
* Are typically source-type specific
* Operate statelessly - all context comes from the video field and observation history

Implemented observers include:
* **Field parity observers**: Determine field parity (odd/even) using ld-decode's algorithm with fallback to previous field
* **PAL phase observers**: Detect PAL phase information (requires field parity)
* **Biphase observers**: Decode biphase-encoded data from VBI lines
* **VITC observers**: Extract VITC (Vertical Interval Timecode) information
* **Closed caption observers**: Extract closed caption data from line 21
* **Video ID observers**: Decode Video ID information
* **FM code observers**: Extract FM code information
* **White flag observers**: Detect white flag presence in VBI
* **VITS observers**: Extract VITS test signal quality metrics
* **Burst level observers**: Measure color burst amplitude levels

### 4.2 Observation History

Some observers require information from previously processed fields (e.g., field parity detection often relies on the previous field's parity). To support this without making observers stateful:

* Observers receive an **ObservationHistory** parameter containing observations from prior fields
* The history flows through the DAG alongside video data via the representation interface
* At sources, observations are read from input metadata (e.g., from .tbc.db files)
* At sinks, observations are pre-populated from all sources before processing begins
* During processing, observations are incrementally added to history for use by subsequent observers

This architecture ensures:
* **Multi-source support**: Merge stages combine observations from all inputs
* **Field reordering**: History remains correct even when field map stages change processing order
* **Observer independence**: No global state, observers remain pure functions
* **Deterministic execution**: Fields processed in sorted FieldID order regardless of DAG structure

### 4.3 Rationale

Some transformations improve video quality and metadata accuracy (e.g. field mapping, dropout correction). By placing observers at node outputs:

* Observers always see the **node output version** of the signal
* Observations change as processing progresses (so improvements or degradations by the nodes can be observed)
* Observations propagate through the DAG to ensure complete history at every stage

---

## 5. Decoder Hints

## 5.1 Use and propagation of hints from the decoder

Since the decoder stage (ld-decode, vhs-decode, etc.) has access to information that is not preserved in the TBC file the decoder provides information that cannot be observed.

Such information is termed "hints" and Decode Orc provides a specific mechanism for propagating such information through the DAG chain.

Examples of hints are dropout, field parity (as half-lines are not represented in TBCs) and PAL field phase.

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

## 11. Structural Transforms

Beyond signal correction, nodes can perform structural operations.

**Currently implemented:**

* **Field mapping**: Remap field IDs to correct skips, jumps, or discontinuities in the source
* **Overwrite**: Replace specific field ranges with data from another source

**Future capabilities may include:**

* Cutting/slicing specific field/frame ranges
* Combining multiple captures into a single logical source

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

## 13. Multi-Project Integration

The orchestrator is designed to be reused across multiple decode projects.

**Integration strategy:**

* The orchestrator can be included as a **submodule** in upstream projects
* Upstream CI pipelines can feed decoded data into the orchestrator DAGs
* CI can verify that upstream changes do not break downstream decoding
* Failures can alert contributors during the PR process

This enables:

* Clear ownership of breaking changes
* Early detection of integration issues
* Long-term stability guarantees

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

# Building and Usage

## 1. Dependencies

**System Requirements (must be installed):**
- **CMake** >= 3.20
- **C++17** compatible compiler (GCC 8+, Clang 7+, MSVC 2019+)
- **spdlog**: C++ logging library (header-only, but installed via package manager)
- **SQLite3**: Database for TBC metadata
- **yaml-cpp**: YAML parsing library
- **libpng**: PNG image library (for preview export)
- **Qt6** (optional, for GUI): Widgets module

**Installation commands:**

Ubuntu/Debian:
```bash
sudo apt install cmake build-essential libspdlog-dev libsqlite3-dev libyaml-cpp-dev libpng-dev qt6-base-dev
```

Fedora/RHEL:
```bash
sudo dnf install cmake gcc-c++ spdlog-devel sqlite-devel yaml-cpp-devel libpng-devel qt6-qtbase-devel
```

macOS (via Homebrew):
```bash
brew install cmake spdlog sqlite yaml-cpp libpng qt@6
```

See [DEPENDENCIES.md](DEPENDENCIES.md) for detailed dependency management information.

## 2. Building

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

**Build options:**
- `-DBUILD_GUI=OFF`: Disable Qt6 GUI (CLI only build)
- `-DBUILD_TESTS=OFF`: Disable test executables

## 3. Running orc-cli

The CLI tool loads an ORC project file and triggers all sink nodes to execute the processing pipeline.

```bash
./bin/orc-cli <project-file> [options]
```

**Options:**
- `--log-level <level>`: Set logging verbosity (trace, debug, info, warn, error, critical, off). Default: info
- `--log-file <file>`: Write logs to specified file (in addition to console)
- `-h, --help`: Display usage information

**Examples:**
```bash
# Run a project
./bin/orc-cli project-examples/test4.orcprj

# Run with debug logging and log file
./bin/orc-cli project.orcprj --log-level debug --log-file output.log
```

## 4. Running orc-gui

The GUI provides an interactive interface for creating and managing ORC projects.

```bash
./bin/orc-gui [project-file]
```

**Options:**
- `--log-level <level>`: Set logging verbosity (trace, debug, info, warn, error, critical, off). Default: info
- `--log-file <file>`: Write logs to specified file (in addition to console)
- `-h, --help`: Display usage information
- `-v, --version`: Display version information

**Examples:**
```bash
# Launch GUI without project
./bin/orc-gui

# Open a specific project
./bin/orc-gui project-examples/test4.orcprj

# Run with trace logging
./bin/orc-gui --log-level trace --log-file gui.log
```

## 5. Logging

Both orc-cli and orc-gui use **spdlog** for structured logging with color-coded console output:

- **trace**: Very detailed execution flow (typically for debugging library internals)
- **debug**: Detailed execution flow (DAG operations, field rendering, etc.)
- **info**: High-level operations (project loading, source addition, etc.) - **default**
- **warn**: Potential issues that don't prevent operation
- **error**: Errors that prevent specific operations
- **critical**: Fatal errors
- **off**: Disable all logging

Qt messages (qDebug, qInfo, qWarning, etc.) in orc-gui are automatically bridged to spdlog and prefixed with `[Qt]`.