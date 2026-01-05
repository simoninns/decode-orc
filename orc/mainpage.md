\mainpage Decode Orc Documentation

# Overview

**Decode Orc** is a cross-platform orchestration and processing framework for LaserDisc and tape decoding workflows.

It brings structure and consistency to complex decoding processes, making them easier to run, repeat, and understand. Decode Orc is a direct replacement for the existing ld-decode-tools, coordinating each step of the process and keeping track of inputs, outputs, and results.

## Key Features

- **DAG-Based Processing**: Processing is described declaratively using a Directed Acyclic Graph (DAG) stored in a project file
- **Field-Level Navigation**: Navigate and process individual video fields with precise control
- **Multi-Source Support**: Combine and align multiple sources through stacking and mapping
- **Observer System**: Extract metadata from video signals without modification
- **Reproducible Workflows**: Project files ensure exact reproduction across runs and versions
- **Dual Interface**: Both GUI (orc-gui) and CLI (orc-cli) use the same core processing library

## Architecture

The system follows an MVP (Model-View-Presenter) architecture:

- **orc-core**: Shared core library containing all processing logic, data models, and DAG execution
- **orc-gui**: Qt6-based graphical interface for interactive project creation and visualization
- **orc-cli**: Command-line interface for batch processing and automation

All business logic resides in the core library, ensuring that project files created in the GUI can be executed unchanged by the CLI.

## Processing Model

### Nodes and the DAG

Processing is performed by **nodes** connected in a DAG. Node types include:
- **Sources**: Ingress from decoders (ld-decode, vhs-decode, etc.)
- **Sinks**: Egress points (output files)
- **Transform nodes**: Signal processing (dropout correction, stacking, filtering, etc.)
- **Structural nodes**: Field mapping, cutting, merging

### Observers

Observers monitor node outputs to extract metadata without modifying the signal:
- Field parity detection
- PAL phase detection
- VBI data extraction (VITC, closed captions, Video ID, etc.)
- Signal quality metrics (VITS, burst levels)

### Decoder Hints

Information that cannot be observed from TBC files (dropout locations, original field parity) is propagated as "hints" through the DAG chain.

## Project Files

Project files (`.orcprj`) describe:
- Source configurations
- DAG structure and node connections
- Node parameters and settings

They do **not** store processing results, ensuring:
- Long-term reproducibility
- Automatic benefit from processing improvements
- Version control compatibility

## Video Format Support

- **PAL** / **PAL-M**
- **NTSC**

Sources can include:
- LaserDisc captures (CAV/CLV)
- Tape-based sources (VHS, etc.)

## Getting Started

### Building the Project

See the main README.md for detailed build instructions. Quick start:

```bash
mkdir build
cd build
cmake ../orc
make -j4
```

### Running the CLI

```bash
./bin/orc-cli <project-file> [options]
```

### Running the GUI

```bash
./bin/orc-gui [project-file]
```

## Key Concepts

- **Field**: The atomic unit of processing - each video field is processed independently
- **Field ID**: Unique sequential identifier for each source field
- **Virtual Field IDs**: Remapped field sequences to correct skips and discontinuities
- **Observation History**: Metadata propagated through the DAG for observer context
- **Stacking**: Combining multiple captures of the same content to improve quality

## Further Information

For detailed architectural documentation, see the main project README.md.

For API documentation, browse the modules and classes in the navigation menu.

---

**Project Status**: Under active development

**License**: GPLv3 (based on the original ld-decode-tools)

**Author**: Simon Inns (2018-2025)
