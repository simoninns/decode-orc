# ld-disc-crapper

This tool generates corrupted TBC files for testing the disc mapper.
It reads clean TBC+metadata files and introduces various types of corruption patterns:
skips, repeats, and gaps that simulate real laserdisc player issues.

Based on the legacy ld-decode TBC library.

## Building

```bash
cd legacy-tools/ld-disc-crapper
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

Use `ld-disc-crapper` to create broken TBC files from clean source data:

```bash
./ld-disc-crapper -p <pattern> <input.tbc> <output.tbc>
```

### Available Corruption Patterns

#### simple-skip
Skip 5 fields every 100 fields - simulates player skipping during playback.

**Example:**
```bash
./ld-disc-crapper -p simple-skip \
    ../../test-data/laserdisc/ntsc/ggv1069/716-914/ggv1069_ntsc_cav_716-914.tbc \
    ../../test-data/maptesting/ggv1069_skip.tbc
```

#### simple-repeat
Repeat 3 fields every 50 fields - simulates player re-reading the same frames.

**Example:**
```bash
./ld-disc-crapper -p simple-repeat \
    ../../test-data/laserdisc/ntsc/bambi/8000-8200/bambi_ntsc_clv_8000-8200.tbc \
    ../../test-data/maptesting/bambi_repeat.tbc
```

#### skip-with-gap
Skip 10 fields and insert 5 black fields every 200 fields - simulates severe tracking loss.

#### heavy-skip
Skip 15 fields every 100 fields - severe damage simulation.

#### heavy-repeat  
Repeat 5 fields every 30 fields - severe sticking simulation.

#### mixed-light
Light mix of skips and repeats - simulates minor disc issues.

#### mixed-heavy
Heavy mix of skips, repeats, and gaps - simulates severe disc damage.

## List Available Patterns

```bash
./ld-disc-crapper --list-patterns
```

## Output

The tool generates:
- Output TBC file with corrupted field data
- Output `.tbc.db` SQLite metadata file with matching field count
- Detailed corruption report showing which frames were affected

Example output:
```
=== Corruption Details ===
(Frame numbers shown - visible in ld-analyse VBI display)

  SKIP: Frames 67 - 69 (5 fields)
  SKIP: Frames 124 - 126 (5 fields)
  REPEAT: Frame 165 (field 329)
```

## Notes

- Generated files have `.tbc` and `.tbc.db` metadata (SQLite format)
- Metadata field count automatically matches TBC file
- Black fields are inserted as zeros (0x0000)
- Corruption uses pseudo-random pattern based on corruption rate
- Frame numbers in output report match ld-analyse VBI display
