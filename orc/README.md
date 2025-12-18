# decode-orc Core Implementation

This directory contains the C++ implementation of decode-orc, following the design specified in `/docs/DESIGN.md`.

## Build Instructions

Out-of-source CMake build:

```bash
mkdir build
cd build
cmake ../orc
make
```

### Build Options

- `BUILD_TESTS` (default: ON) - Build unit tests
- `BUILD_GUI` (default: OFF) - Build Qt6 GUI applications (requires Qt6)

Example:

```bash
cmake ../orc -DBUILD_GUI=ON -DBUILD_TESTS=ON
make
```

## Project Structure

```
orc/
├── core/           # Core library (libOrc-core.a)
│   ├── include/    # Public headers
│   └── *.cpp       # Implementation files
├── cli/            # Command-line tools
├── gui/            # Qt6 GUI applications
└── tests/          # Unit tests
```

## Implementation Status

### Phase 1: Foundations ✅

- [x] FieldID model ([field_id.h](core/include/field_id.h))
- [x] Video field representation abstraction ([video_field_representation.h](core/include/video_field_representation.h))
- [x] Artifact identity and provenance ([artifact.h](core/include/artifact.h))
- [x] Minimal DAG executor ([dag_executor.h](core/include/dag_executor.h))
- [x] **TBC file reader** ([tbc_reader.h](core/include/tbc_reader.h))
- [x] **TBC metadata reader (SQLite)** ([tbc_metadata.h](core/include/tbc_metadata.h))
- [x] **TBC VideoFieldRepresentation implementation** ([tbc_video_field_representation.h](core/include/tbc_video_field_representation.h))

### Phase 2: Observers ✅

- [x] Observer framework ([observer.h](core/include/observer.h))
- [x] VBI observers (6 types):
  - [x] BiphaseObserver ([biphase_observer.h](core/include/biphase_observer.h))
  - [x] VitcObserver ([vitc_observer.h](core/include/vitc_observer.h))
  - [x] ClosedCaptionObserver ([closed_caption_observer.h](core/include/closed_caption_observer.h))
  - [x] VideoIdObserver ([video_id_observer.h](core/include/video_id_observer.h))
  - [x] FmCodeObserver ([fm_code_observer.h](core/include/fm_code_observer.h))
  - [x] WhiteFlagObserver ([white_flag_observer.h](core/include/white_flag_observer.h))
- [x] VITS Quality Observer ([vits_observer.h](core/include/vits_observer.h))
- [x] orc-process CLI tool with YAML pipeline support

### Phase 3: Dropout Detection and Correction (Not Started)

- [ ] Dropout observer
- [ ] Hint ingestion
- [ ] Dropout correction stage

### Phase 3+: To Be Implemented

See `/docs/DESIGN.md` Section 10 for full roadmap.

## Running Tests

```bash
cd build
make test
# or run directly:
./bin/orc-tests
```

## Design Principles

This implementation adheres to the architectural principles in `/docs/DESIGN.md`:

1. **FieldID as fundamental coordinate system** - All time-varying data uses FieldID
2. **Three-class processing model** - Signal Transformers, Observers, Decisions
3. **Immutable artifacts** - All processing results are immutable with provenance
4. **Single DAG per run** - Static, declarative processing pipeline

## Dependencies

- C++17 or later
- CMake 3.20+
- SQLite3
- Qt6 (optional, for GUI)

## License

See `/LICENSE` in the repository root.
