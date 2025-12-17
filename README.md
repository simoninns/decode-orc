# ld-decode-orc

**ld-decode-orc** is a cross-platform orchestration and processing framework for LaserDisc decoding workflows.

![](./assets/ld-decode-orc-icon-small.png)

It aims to brings structure and consistency to complex LaserDisc decoding processes, making them easier to run, repeat, and understand.

`ld-decode-orc` is a direct replacement for the existing ld-decode-tools, coordinating each step of the process and keeping track of inputs, outputs, and results.

The project aims to:
- Make advanced LaserDisc workflows (from TBC to chroma) easier to manage
- Reduce manual steps and error-prone command sequences
- Help users reproduce the same results over time

A graphical interface is also planned, alongside support for automated and scripted workflows.

This project is under active development.

## Current Status

### ✅ Phase 1: Core Framework (Completed)
- FieldID coordinate system
- Video field representation abstraction
- Artifact identity and provenance
- DAG executor with caching
- TBC file I/O
- SQLite metadata reader

### ✅ Phase 2: Observers (Completed)
- Observer framework
- 6 VBI observers (Biphase, VITC, ClosedCaption, VideoId, FmCode, WhiteFlag)
- VITS Quality Observer (SNR/PSNR measurements)
- **orc-process** CLI tool with YAML pipeline support
- Complete metadata regeneration (SQLite)
- Tested with 17 reference TBC files (9 PAL, 8 NTSC)

### 🚧 Phase 3: Dropout Detection (In Progress)
- Dropout observer
- Hint ingestion
- Dropout correction stage

## Quick Start

### Build

```bash
mkdir build
cd build
cmake ../orc
make -j$(nproc)
```

### Process a TBC File

```bash
build/bin/orc-process --dag examples/vbi-observers.yaml \
  test-data/ntsc/reference/cinder/9000-9210/cinder_ntsc_clv_9000-9210.tbc \
  output/cinder_processed.tbc
```

This will:
1. Load the input TBC and its metadata
2. Execute all 7 observers on each field
3. Generate new SQLite metadata with all observations
4. Output a processed TBC compatible with `ld-analyse`

### View Results

```bash
ld-analyse output/cinder_processed.tbc
```

## Documentation

- [Design Overview](docs/DESIGN.md) - Architecture and principles
- [Data Model](docs/DATA-MODEL.md) - FieldID, artifacts, representations
- [DAG Format](docs/DAG.md) - Pipeline definition specification
- [orc-process Tool](orc/cli/README.md) - CLI tool documentation
- [Test Data Inventory](test-data/TEST_DATA_INVENTORY.md) - Available test files

## Project Structure

```
ld-decode-orc/
├── orc/                    # Core C++ implementation
│   ├── core/               # libOrc-core.a
│   │   ├── include/        # Public API headers
│   │   └── *.cpp           # Implementation
│   ├── cli/                # Command-line tools
│   │   └── orc-process     # Observer pipeline executor
│   └── tests/              # Unit tests
├── examples/               # Example pipelines
│   ├── vbi-observers.yaml  # All 7 observers
│   └── biphase-only.yaml   # Minimal example
├── test-data/              # Test TBC files
│   ├── pal/                # PAL test files
│   └── ntsc/               # NTSC test files
├── docs/                   # Architecture documentation
└── legacy-tools/           # Original ld-decode-tools
```
