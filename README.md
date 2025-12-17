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

### ✅ Phase 3: Dropout Detection and Correction (Completed)
- User decision framework (ADD/REMOVE/MODIFY dropouts)
- Dropout correction stage with intra/interfield options
- Uses TBC metadata hints directly (no separate observer needed)
- Quality-based replacement line selection
- Multiple corrections per line support
- **orc-process** stage execution with TBC regeneration
- Legacy-compatible correction parameters
- Metadata tracking (dropout table reflects correction state)
- Tested with 400-field PAL capture (286 fields corrected, 550 dropouts)
- Processing time: ~0.5 seconds for 400 fields

### 🚧 Phase 4: Multi-source and Export (Planned)
- Field fingerprinting and alignment
- Multi-source stacking stage
- Export stage (video/metadata/audio)

## Quick Start

### Build

```bash
mkdir build
cd build
cmake ../orc
make -j$(nproc)
```

### Process a TBC File with VBI Extraction

```bash
build/bin/orc-process --dag examples/vbi-observers.yaml \
  test-data/pal/reference/ggv1011/1005-1205/ggv1011_pal_cav_1005-1205.tbc \
  output/ggv1011_processed.tbc
```

This will:
1. Load the input TBC and its metadata
2. Execute all 7 observers on each field
3. Generate new SQLite metadata with all observations
4. Output a processed TBC compatible with `ld-analyse`

### Correct Dropouts

```bash
build/bin/orc-process --dag examples/dropout-correct.yaml \
  input.tbc \
  output/corrected.tbc
```

This will:
1. Load dropout hints from input TBC metadata
2. Correct dropouts using intra/interfield line replacement
3. Extract VBI metadata from corrected fields
4. Generate corrected TBC and metadata (0 dropouts)

**Performance**: ~0.5 seconds for 400 PAL fields

### Correct Dropouts with Manual Decisions

```bash
build/bin/orc-process --dag examples/dropout-correct-with-decisions.yaml \
  input.tbc \
  output/corrected.tbc
```

The YAML file can include manual dropout decisions:
```yaml
dropout_decisions:
  - field_id: 42
    action: ADD
    line: 198
    start_sample: 100
    end_sample: 150
    notes: "Missed by detector"
```

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
