# NTSC Test Data

This directory contains NTSC format test files.

## NTSC Characteristics

- **Lines per field**: 262.5 (525 lines per frame)
- **Field rate**: ~59.94 Hz (60/1.001)
- **Frame rate**: ~29.97 Hz (30/1.001)
- **Color encoding**: NTSC (composite quadrature modulation)
- **Horizontal frequency**: ~15.734 kHz
- **Common sample rates**: ~14.3 MHz (4fsc), ~28 MHz

## Subdirectories

- **small/** - Small NTSC files for quick unit tests (<100MB, <1000 fields)
- **reference/** - Reference NTSC captures for validation and regression testing
- **problematic/** - NTSC files with known issues (dropouts, skips, damage)
- **multi-source/** - Multiple NTSC captures of same content for stacking tests

## Common NTSC Sources

- North American LaserDiscs
- Japanese LaserDiscs
- US/Canadian/Mexican broadcasts
