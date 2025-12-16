# PAL Test Data

This directory contains PAL format test files.

## PAL Characteristics

- **Lines per field**: 312.5 (625 lines per frame)
- **Field rate**: 50 Hz
- **Frame rate**: 25 Hz
- **Color encoding**: PAL (Phase Alternating Line)
- **Horizontal frequency**: 15.625 kHz
- **Common sample rates**: ~17.7 MHz (4fsc), ~28 MHz

## Subdirectories

- **small/** - Small PAL files for quick unit tests (<100MB, <1000 fields)
- **reference/** - Reference PAL captures for validation and regression testing
- **problematic/** - PAL files with known issues (dropouts, skips, damage)
- **multi-source/** - Multiple PAL captures of same content for stacking tests

## Common PAL Sources

- European LaserDiscs
- UK/European broadcasts
- SECAM regions (though SECAM discs are rare)
