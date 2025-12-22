# orc-chroma-decoder

This is an independent build of the ld-chroma-decoder tool, extracted from the legacy-tools directory as a first step toward integrating it as a sink node in orc-core.

## Directory Structure

- `src/` - Main chroma decoder source code (copied from legacy-tools/ld-chroma-decoder)
- `lib/tbc/` - TBC library files (copied from legacy-tools/library/tbc)
- `lib/filter/` - Filter library headers (copied from legacy-tools/library/filter)

## Building

This is now an independent build that doesn't rely on the other legacy tools:

```bash
mkdir build
cd build
cmake ..
make
```

## Dependencies

- Qt6 (Core and Sql modules)
- FFTW3 library

## Next Steps

This will be refactored into an integrated sink node for orc-core, allowing chroma decoding to be performed as part of the ORC processing pipeline.
