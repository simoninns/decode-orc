# Source Stage Shared Components

This subtree groups shared code used by source stages to make interdependence explicit.

## Common Components

- `common/tbc_source_loader.h`
  - Shared plugin-side loader helpers for TBC-backed source formats.
  - Keeps format-specific file I/O local to source-stage implementation paths.

## Current Source Stage Consumers

- `../ntsc_comp_source`
- `../ntsc_yc_source`
- `../pal_comp_source`
- `../pal_yc_source`

These stages intentionally share the same TBC loading helper while preserving
stage-specific parameter validation and system-specific behavior.
