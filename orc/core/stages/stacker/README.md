# Stacker Stage

## Overview

The Stacker stage is a Many-to-One (MERGER type) processing node that combines multiple TBC captures of the same LaserDisc to produce a superior output. It implements the functionality from the legacy `ld-disc-stacker` tool.

## Purpose

By analyzing corresponding fields from multiple sources, the Stacker selects the best data for each field, effectively:
- Reducing dropouts
- Improving overall signal quality
- Combining the best parts of multiple captures into one optimal output

## Node Type

**Type:** MERGER  
**Inputs:** 1-16 video field streams  
**Outputs:** 1 combined video field stream  

**Note:** When only 1 input is provided, the stage operates in passthrough mode and simply returns the input unchanged.  

## Parameters

### Mode
**Name:** `mode`  
**Type:** Integer (-1 to 4)  
**Default:** -1 (Auto)  

Selects the stacking algorithm:
- **-1 (Auto):** Automatically selects best mode based on number of sources
  - 1 source: Passthrough (no stacking)
  - 2 sources: Uses Mean mode
  - 3+ sources: Uses Smart Mean mode
- **0 (Mean):** Simple averaging of all source values
- **1 (Median):** Median value of all sources
- **2 (Smart Mean):** Mean of values within threshold distance from median
  - More robust to outliers than simple mean
  - Uses `smart_threshold` parameter
- **3 (Smart Neighbor):** Uses neighboring pixels to guide selection
  - Considers North, South, East, West pixels
  - Uses `smart_threshold` parameter
- **4 (Neighbor):** Context-aware selection using neighboring pixels
  - Similar to Smart Neighbor but with different weighting

### Smart Threshold
**Name:** `smart_threshold`  
**Type:** Integer (0-128)  
**Default:** 15  

Range of value in 8-bit units for smart mode selection. Controls how close values must be to the median to be included in smart mean calculation. Lower values are more selective; higher values include more sources.

### Disable Differential Dropout Detection
**Name:** `no_diff_dod`  
**Type:** Boolean  
**Default:** false  

When false, the stage uses differential dropout detection to identify false positive dropout markings. When enabled with 3+ sources, it can recover pixels that were incorrectly marked as dropouts by the decoder.

### Passthrough
**Name:** `passthrough`  
**Type:** Boolean  
**Default:** false  

When true, dropouts that are present on ALL sources are passed through to the output. This preserves areas where every source has issues.

### Reverse Field Order
**Name:** `reverse`  
**Type:** Boolean  
**Default:** false  

Reverses the field order from first/second to second/first. Used when source captures have different field ordering.

## Usage Examples

### Passthrough Mode (Single Source)
```
Source1 ──> Stacker ──> Output
```
When only one source is provided, the Stacker passes it through unchanged.

### Basic Two-Source Stack
```
Source1 ──┐
          ├──> Stacker (Auto mode) ──> Output
Source2 ──┘
```

### Three-Source with Smart Mean
```
Source1 ──┐
Source2 ──┼──> Stacker (Mode=2, Threshold=20) ──> Output
Source3 ──┘
```

### Maximum Stack (16 sources)
```
Source1  ──┐
Source2  ──┤
Source3  ──┤
Source4  ──┤
Source5  ──┤
Source6  ──┤
Source7  ──┤
Source8  ──┼──> Stacker (Mode=2, Smart Neighbor) ──> Output
Source9  ──┤
Source10 ──┤
Source11 ──┤
Source12 ──┤
Source13 ──┤
Source14 ──┤
Source15 ──┤
Source16 ──┘
```

## Requirements

All input sources must:
- Be from the same physical disc (same content)
- Have the same video format (PAL with PAL, NTSC with NTSC)
- Have the same field range (start and end field IDs)
- Have undergone VBI processing for best results
- Contain dropout metadata and quality information

## Implementation Status

**Current Status:** Base implementation complete

The current implementation provides:
- Parameter configuration and validation
- Input/output handling
- Basic stacking modes (Mean, Median, Smart Mean)
- Dropout detection framework

**TODO for full feature parity:**
- Complete neighbor-based stacking modes (3 and 4)
- Full differential dropout detection implementation
- Source quality tracking and output metadata
- Performance optimization for multi-threaded processing

## Legacy Tool Comparison

This stage replaces the standalone `ld-disc-stacker` command-line tool with equivalent functionality integrated into the ORC DAG pipeline. Key improvements:
- Visual DAG integration
- Real-time parameter adjustment
- Composable with other processing stages
- Project-based workflow

## Files

- `stacker_stage.h` - Stage interface and declarations
- `stacker_stage.cpp` - Implementation
- `README.md` - This documentation

## See Also

- Legacy tool: `legacy-tools/ld-disc-stacker/`
- Related stages: `passthrough_merger` (simple merger example)
- Documentation: `docs/DESIGN.md`, `docs/DAG-FIELD-RENDERING.md`
