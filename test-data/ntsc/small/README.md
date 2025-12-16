# Small NTSC Test Files

Small NTSC format TBC files suitable for quick unit tests and continuous integration.

## Recommended Size

Files in this directory should be:
- < 100 MB each
- < 1000 fields (approximately 8-16 seconds of video)
- Suitable for automated testing

## Purpose

Use these for:
- Unit tests that need real TBC data
- Quick validation of processing stages
- CI/CD pipeline testing
- Development iteration

## Example Files

- `bars_and_tone.tbc` - NTSC color bars and test tone
- `black_frame.tbc` - Pure black field(s) for baseline testing
- `white_frame.tbc` - Pure white field(s) for saturation testing
- `gradient.tbc` - Luma gradient pattern
