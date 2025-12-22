# ORC Chroma Decoder Test Suite

Comprehensive test suite for the orc-chroma-decoder to ensure output consistency during refactoring.

## Overview

This test suite validates that the chroma decoder produces consistent output across:
- **PAL decoders**: pal2d, transform2d, transform3d
- **NTSC decoders**: ntsc1d, ntsc2d, ntsc3d, ntsc3dnoadapt
- **Output formats**: RGB, YUV, Y4M
- **Processing options**: Chroma gain/phase, noise reduction, field order, line ranges
- **Monochrome output**: Black and white processing

The tests use real laserdisc test data (PAL and NTSC) and verify output by comparing SHA-256 checksums against baseline signatures stored in a single manifest file.

## Quick Start

### 1. Build the Decoder

```bash
cd /path/to/orc-chroma-decoder
mkdir -p build && cd build
cmake ..
make
```

### 2. Generate Baseline Signatures (First Time)

```bash
cd /path/to/orc-chroma-decoder/tests
./run-tests.sh generate
```

This creates `references/test-signatures.txt` with checksums for all test outputs.

### 3. Verify Against Baseline

```bash
./run-tests.sh verify
# or simply
./run-tests.sh
```

### 4. Quick Smoke Test

```bash
./run-tests.sh verify --quick
```

## Test Modes

### Generate Mode

Generate baseline signatures from current decoder output:

```bash
./run-tests.sh generate
```

This runs all tests and saves output checksums to `references/test-signatures.txt`.

### Verify Mode (Default)

Verify decoder output against baseline signatures:

```bash
./run-tests.sh verify
# or simply
./run-tests.sh
```

This runs all tests and compares output checksums against the baseline.

### Quick Mode

Run only essential tests for rapid validation:

```bash
./run-tests.sh verify --quick
./run-tests.sh generate --quick
```

### Verbose Mode

Shows detailed output during test execution:

```bash
./run-tests.sh verify --verbose
```

### Run SpecifBaseline Signatures

**Important**: Only generate baseline signatures after manually verifying the decoder output is correct!

### First Time Setup

1. Build the decoder
2. Generate baseline signatures:

```bash
./run-tests.sh generate
```

or use the helper script:

```bash
./generate-references.sh
```

3. Manually verify some outputs to ensure quality:

```bash
# Example: View an output with ffplay or similar
ffplay -f rawvideo -pixel_format rgb48le -video_size 928x576 -framerate 25 outputs/pal_2d_rgb.rgb
```

4. If output looks correct, commit the signatures:

```bash
git add references/test-signatures.txt
git commit -m "Add baseline signatures for chroma decoder tests"
```

### Workflow

The typical workflow for using the test suite:

```bash
# 1. Initial setup - generate baseline
./run-tests.sh generate

# 2. Make code changes to the decoder
# ... edit source files ...

# 3. Rebuild
cd ../build && make && cd ../tests

# 4. Verify nothing broke
./run-tests.sh verify

# 5. If tests fail but changes are intentional, regenerate baseline
./run-tests.sh generate
```

### Updating Signatures After Intentional Changes

If you intentionally modify the decoder behavior:

1. Run tests to see what changed:

```bash
./run-tests.sh verify
```

2. Verify the new output is correct (check a few manually)
3. Regenerate baseline signatures:

```bash
./run-tests.sh generate
```

4. Review and commit the changes:

```bash
git diff references/test-signatures.txt
git add references/test-signatures.txt
git commit -m "Update signatures after decoder improvement"
```decoder behavior:

1. Run tests to see what changed:

```bash
./run-tests.sh
```

2. Verify the new output is correct
3. Update references:

```bash
./generate-references.sh
```

4. Review and commit the changes

## Test Data

Tests use real laserdisc TBC files from `../../test-data/laserdisc/`:

### PAL Sources
- **amawaab**: PAL CLV, frames 6001-6205 (primary test source)
- **ggv1011**: PAL CAV, multiple frame ranges
- **gpblank**: PAL test patterns

### NTSC Sources
- **bambi**: NTSC CLV, frames 18100-18306 (primary test source)
- **ggv1069**: NTSC CAV, multiple frame ranges
- **appleva**: NTSC test content
- **cinder**: NTSC test content

## Test Categories

### Basic Decoder Tests
- PAL 2D decoder (default PAL)Helper script for generating baseline
├── test-config.ini          # Test case definitions (documentation)
├── README.md                # This file
├── Makefile                 # Convenience make targets
├── references/              # Baseline signatures
│   └── test-signatures.txt  # Single file with all test signatures
├── outputs/                 # Test output files (gitignored)
│   ├── pal_2d_rgb.rgb
│   ├── ntsc_2d_rgb.rgb
│   └── ...
└── temp/                    # Temporary files and logs (gitignored)
    └── *.log
```

## Signature File Format

The `references/test-signatures.txt` file contains one line per test:

```
TESMODE`: Set to 'generate' or 'verify'
- `QUICK_MODE`: Set to 1 for quick test mode

Example:

```bash
VERBOSE=1 DECODER=/custom/path/to/decoder ./run-tests.sh verify
- Track which tests changed
- Understand test coverage at a glance
### Processing Options Tests
- Chroma gain adjustment
- Chroma phase rotation
- Chroma noise reduction (NTSC)
- Luma noise reduction
- NTSC phase compensation
- Simple PAL mode (1D UV filter)

### Edge Case Tests
- Reversed field order
- Output padding
- Custom line ranges
- CAV vs CLV sources

## Directory Structure

```
tests/
├── run-tests.sh              # Main test runner
├── generate-references.sh    # Reference checksum generator
├── test-config.ini          # Test case definitions
├── README.md                # This file
├── references/              # Reference SHA-256 checksums
│   ├── PAL_2D_RGB.sha256
│   ├── NTSC_2D_RGB.sha256
│   └── ...
├── outputs/                 # Test output files (gitignored)
│   ├── pal_2d_rgb.rgb
│   ├── ntsc_2d_rgb.rgb
│   └── ...
└── temp/                    # Temporary files and logs (gitignored)
    └── *.log
```

## Environment Variables

- `DECODER`: Path to decoder executable (default: `../build/bin/orc-chroma-decoder`)
- `VERBOSE`: Set to 1 for verbose output
- `UPDATE_REFERENCES`: Set to 1 to update reference checksums
- `QUICK_MODE`: Set to 1 for quick test mode

Example:

```bash
VERBOSE=1 DECODER=/custom/path/to/decoder ./run-tests.sh
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Chroma Decoder Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
        with:
          lfs: true  # Pull test data if using Git LFS
      
      - name: Install Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake qt6-base-dev libfftw3-dev
      
      - name: Build Decoder
        run: |
          cd orc-chroma-decoder
          mkdir -p build && cd build
          cmake ..
          make
      
      - name: Run Quick Tests
        run: |
          cd orc-chroma-decoder/tests
          ./run-tests.sh --quick
      
      - name: Run Full Tests
        run: |
          cd orc-chroma-decoder/tests
          ./run-tests.sh
```

### GitLab CI Example

```yaml
test:chroma-decoder:
  stage: test
  script:
    - cd orc-chroma-decoder
    - mkdir -p build && cd build
    - cmake ..
    - make
    - cd ../tests
    - ./run-tests.sh
  artifacts:
    when: on_failure
    paths:
      - orc-chroma-decoder/tests/temp/*.log
```

## Interpreting Results

### Successful Test

```
[INFO] Running test: PAL_2D_RGB
[PASS] Generated signature: PAL_2D_RGB    # in generate mode
# or
[PASS] Test passed: PAL_2D_RGB            # in verify mode
```

### Failed Test (Signature Mismatch)

```
[INFO] Running test: PAL_2D_RGB
[FAIL] Signature mismatch: PAL_2D_RGB
[FAIL]   Expected: abc123...
[FAIL]   Got:      def456...
```

This indicatBaseline

```
[INFO] Running test: NEW_TEST
[WARN] No baseline signature found for: NEW_TEST
[INFO] Run in 'generate' mode to create baseline
```

New tests need baseline signatures generated.

## Troubleshooting

### No Signatures File Found

```
[WARN] No signatures file found: references/test-signatures.txt
[INFO] Run in 'generate' mode first to create baseline signatures
```

**Solution**: Generate baseline signatures first:

```bash
./run-tests.sh generate
```erence checksums generated.

## Troubleshooting

### Test Data Not Found

```
[FAIL] Input file not found: /path/to/test.tbc
```

**Solution**: Ensure test data is available at `../../test-data/laserdisc/`

### Decoder Not Built
baseline signatures:

```bash
./run-tests.sh generate
```

### Signature Mismatch After Refactoring

This is exactly what the tests are for! If you've refactored code and tests fail:

1. Check if the change is expected or a regression
2. If it's a regression, fix the code
3. If it's intentional (e.g., improvement), regenerate baseline:

```bash
./run-tests.sh generate

**Soluta new `run_test` call in `run-tests.sh` (in the appropriate test suite function)
2. Run in generate mode to create baseline:

```bash
./run-tests.sh generate
```

3. Verify the output manually
4. Commit the updated signatures file All Tests Skipped

**Solution**: Generate reference checksums:

```bash
./generate-references.sh
```

## Performance Notes

- Full test suite: ~5-10 minutes (depends on CPU)
- Quick test suite: ~1-2 minutes
- Each test processes 5-10 frames to balance speed vs. coverage
- Output files are ~10-50 MB per test

## Adding New Tests

1. Add test case to `test-config.ini` (if using config-based approach)
2. Or modify `run-tests.sh` to add a new `run_test` call
3. Run the test to generate output
4. Verify output manually
5. Generate reference checksum:

```bash
./generate-references.sh
```

## Tips

- Use `--quick` for rapid iteration during development
- Use `--verbose` when debugging test failures
- Use `--test PATTERN` to focus on specific areas
- Keep reference checksums in version control
- Review checksum changes in pull requests carefully
- Document any intentional output changes in commit messages

## License

Same as orc-chroma-decoder (GPLv3)
