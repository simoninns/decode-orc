# TBC Source Internal Headers

This directory contains **PRIVATE** implementation headers for TBC source stages.

## CRITICAL: Do NOT include these headers outside of source stages!

These headers define the concrete TBC-specific implementations and supporting utilities:
- `tbc_video_field_representation.h` - Composite TBC sources
- `tbc_yc_video_field_representation.h` - YC (separate Y/C) TBC sources
- `tbc_metadata.h` - TBC file metadata structures
- `tbc_audio_efm_handler.h` - TBC audio/EFM file loading

## Architectural Boundary

**TBC** = Time Base Corrector file format (.tbc, .tbcy, .tbcc) - a specific ingestion format

**VFR** = Video Field Representation - the abstract interface used throughout the DAG

### Who can use these headers:

✅ **SOURCE STAGES ONLY:**
- `pal_comp_source/`
- `ntsc_comp_source/`
- `pal_yc_source/`
- `ntsc_yc_source/`

These stages ingest TBC files and expose them through the VFR interface.

### Who CANNOT use these headers:

❌ **Transform stages** - Use `VideoFieldRepresentation` interface only
❌ **Sink stages** - Use `VideoFieldRepresentation` interface only  
❌ **Observers** - Use `VideoFieldRepresentation` interface only
❌ **Any other code** - Use `VideoFieldRepresentation` interface only

## Why This Matters

The VFR can be modified by any stage in the DAG (e.g., dropout correction, stacking).
If a downstream stage accessed TBC metadata directly, it would bypass these modifications
and use stale data from the original TBC file.

**ALL** data flow must go through the VFR interface:
- Video samples via `get_line()`, `get_field()`
- Metadata via `get_video_parameters()`
- Hints via `get_*_hint()` methods
- Observations via `get_observations()`

This ensures that each stage sees the **current** state of the VFR, not the original TBC.
