# encode-orc Test Suite

This directory contains YAML project files for testing encode-orc encoding functionality. The tests generate TBC (Time Base Corrected) video files with various encodings and effects for decode-orc testing.

## Overview

- **encode-projects/**: Contains categorized YAML project files for different test scenarios
  - `dropout-correction/`: Tests for dropout correction algorithms
  - `generic/`: Generic video encoding tests
  - `source-alignment/`: LaserDisc source alignment tests
  - `stacking/`: Multi-field stacking tests
  - `vectorscope/`: Color accuracy and vectorscope calibration tests

- **encode-output/**: Output directory where generated TBC files are written
- **encode-tests.sh**: Test runner script

## Environment Variables

The YAML projects use environment variables for portability and flexibility:

### ENCODE_ORC_ASSETS
Controls the location of testcard images and assets used in encoding projects.

- If set: Uses the specified directory
- If not set: Falls back to local development location (`../external/encode-orc/testcard-images`)
- Default (nix installation): Uses system-installed assets from the nix store

### ENCODE_ORC_OUTPUT_ROOT
Controls where output TBC files are written.

- Automatically set by the test script to `./encode-output/`
- Can be overridden by setting the environment variable before running tests

Example:
```bash
export ENCODE_ORC_OUTPUT_ROOT=/tmp/test-output
./encode-tests.sh
```

## Running Tests

### Prerequisites

1. encode-orc must be installed and available on `$PATH`
   - Via nix: `nix-shell` or flake environment
   - Via system package manager
   - Via manual installation

2. Testcard images must be available
   - Either via nix installation
   - Or via local `../external/encode-orc/testcard-images` directory

### Running All Tests

```bash
./encode-tests.sh
```

The script will:
1. Validate encode-orc is available
2. Set up environment variables
3. Run each YAML project file
4. Report results with file sizes

### Custom Output Location

```bash
export ENCODE_ORC_OUTPUT_ROOT=/custom/output/path
./encode-tests.sh
```

### Custom Asset Location

```bash
export ENCODE_ORC_ASSETS=/path/to/testcard-images
./encode-tests.sh
```

## YAML Project Format

All project files use the encode-orc YAML format with environment variable substitution:

```yaml
output:
  filename: "${ENCODE_ORC_OUTPUT_ROOT}/category/project-name"
  format: "pal-composite"  # or ntsc-composite, pal-yc, ntsc-yc

sections:
  - name: "Content"
    duration: 100
    source:
      type: "yuv422-image"
      file: "${ENCODE_ORC_ASSETS}/pal-raw/625_50_75_BARS.raw"
```

For detailed YAML configuration documentation, see:
https://simoninns.github.io/decode-orc-docs/encode-orc/user-guide/project-yaml/

## Nix Integration

When using the nix flake, assets and encode-orc are automatically configured:

```bash
nix develop
./encode-tests.sh
```

The nix environment provides:
- encode-orc executable
- Testcard images in the nix store
- Proper environment variable configuration

## Troubleshooting

### "encode-orc executable not found"

Ensure encode-orc is installed and on PATH:
```bash
which encode-orc
```

### "encode-orc test assets not found"

If using local fallback assets, ensure the directory exists:
```bash
ls ../external/encode-orc/testcard-images/
```

Or set ENCODE_ORC_ASSETS explicitly:
```bash
export ENCODE_ORC_ASSETS=/path/to/testcard-images
./encode-tests.sh
```

### Tests fail with "file not found"

Verify asset paths are accessible:
```bash
echo $ENCODE_ORC_ASSETS
ls $ENCODE_ORC_ASSETS/pal-raw/625_50_75_BARS.raw
```

## Output Files

Test results are organized by category in `encode-output/`:

```
encode-output/
├── dropout-correction/
│   ├── pal-docorr-test.tbc
│   ├── pal-docorr-test.tbc.json
│   └── ...
├── generic/
├── source-alignment/
├── stacking/
└── vectorscope/
```

Each TBC file includes:
- `.tbc` or `.tbcy`/`.tbcc` (for Y/C): Video data
- `.tbc.json`: Metadata (LD-Decode format)
