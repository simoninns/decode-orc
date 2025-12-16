# NTSC Reference Test Files

Reference NTSC captures with known characteristics for validation.

## Purpose

Use these for:
- Regression testing
- Validation of output quality
- Benchmarking processing stages
- Comparing against legacy ld-decode results

## Documentation Required

Each reference file should have accompanying documentation (`.txt` or `.md` sidecar) describing:
- Expected output characteristics
- Known metadata values (frame counts, VBI data, timecode, closed captions, etc.)
- Processing parameters that should be used
- Expected processing time benchmarks
- Source disc information (title, side, CAV/CLV)
- Known good output checksums or frame hashes
- NTSC-specific characteristics (closed captions, XDS data if present)
