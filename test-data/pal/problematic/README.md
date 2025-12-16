# Problematic PAL Test Files

PAL TBC files with known issues for testing error handling and correction stages.

## Issue Categories

### Dropouts
- `*_dropout_*.tbc` - Files with various dropout patterns
- Test dropout detection and correction algorithms
- Should include light, moderate, and severe dropout examples

### Skips and Jumps
- `*_skip_*.tbc` - Files with player skips or tracking errors
- Test alignment and field sequencing
- Test VBI continuity detection

### Damaged Content
- `*_damaged_*.tbc` - Physically damaged disc captures
- Test robustness and partial recovery
- Includes scratches, disc rot, delamination effects

### Format Issues
- `*_mixed_*.tbc` - Mixed formats or non-standard signals
- Test format detection and handling
- Edge cases and format variations

### VBI Issues
- `*_vbi_*.tbc` - Problematic VBI data (missing, corrupted, inconsistent)
- Test VBI decoder robustness
- PAL-specific VBI issues (teletext errors, WSS problems)

## Documentation

Each file MUST have a sidecar `.txt` or `.md` file documenting:
- The specific issue(s) present
- Where in the file the issues occur (field ranges or timecode)
- Expected behavior of processing stages
- Known limitations or challenges
- Whether the issue is correctable or should be flagged
