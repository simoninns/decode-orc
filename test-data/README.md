# Test Data

This directory contains test TBC (Time Base Corrected) files and their associated metadata for testing ld-decode-orc components.

> **📋 For a complete inventory of all test files and their VBI data content, see [TEST_DATA_INVENTORY.md](TEST_DATA_INVENTORY.md)**

## Structure

Each test case should include:
- `.tbc` - Raw video field data
- `.tbc.json` - JSON metadata (legacy ld-decode format)
- `.db` - SQLite metadata database (if applicable)

## Test Data Organization

Test data is organized first by video format, then by purpose:

```
test-data/
├── pal/
│   ├── small/          # Small PAL test files for quick unit tests
│   ├── reference/      # PAL reference captures for validation
│   ├── problematic/    # PAL files with known issues
│   └── multi-source/   # Multiple PAL captures for stacking
└── ntsc/
    ├── small/          # Small NTSC test files for quick unit tests
    ├── reference/      # NTSC reference captures for validation
    ├── problematic/    # NTSC files with known issues
    └── multi-source/   # Multiple NTSC captures for stacking
```

## Usage

Test data files are **not committed to the repository** due to their size. 
 (format is indicated by directory):

```
{source}_{description}.tbc
```

Examples:
- `test_pattern_clean.tbc` - Clean test pattern
- `movie_dropout.tbc` - Movie with dropouts
- `documentary_a.tbc` - First source for stacking
- `
{source}_{format}_{description}.tbc
```

Examples:
- `pal_test_pattern_clean.tbc` - Clean PAL test pattern
- `ntsc_movie_dropout.tbc` - NTSC movie with dropouts
- `pal_documentary_a.tbc` - First source for stacking
- `pal_documentary_b.tbc` - Second source for stacking

## Required Metadata

Each TBC file should be accompanied by:
- Video format (PAL/NTSC)
- Field count
- Sample rate
- Line length
- Known issues or characteristics
- Source/provenance information

Consider maintaining a `catalog.json` or similar index file describing all test data.
