# Disc Mapper Porting Strategy

## Overview

This document outlines the strategy for porting `ld-discmap` functionality from legacy-tools to decode-orc's core architecture while maintaining architectural principles.

## Executive Summary

**Legacy Tool**: `ld-discmap` analyzes VBI metadata and video characteristics to detect and correct field ordering issues caused by laserdisc player skips, jumps, repeats, and playback failures. It produces a corrected TBC file with fields in proper order.

**Core Challenge**: The legacy tool operates on the entire source at once, building a complete frame map, sorting it, padding gaps, and writing a new TBC. This conflicts with decode-orc's single-field execution model for preview and DAG-based architecture.

**Proposed Solution**: Split disc mapping into THREE distinct components:
1. **Observers** - Analyze individual fields to extract VBI data and quality metrics
2. **Policy/Decision Module** - Analyze complete observation set to generate field mapping decisions
3. **Field Map Stage** - Apply the mapping as a DAG transformation

---

## Legacy Tool Analysis

### What ld-discmap Does

1. **Reads metadata** from source TBC and extracts:
   - VBI frame numbers (biphase-encoded picture numbers)
   - Field phase information (PAL 8-field, NTSC 4-field sequences)
   - Picture stop flags
   - Pulldown frame detection (NTSC CAV)
   - Lead-in/out markers
   - Frame quality metrics

2. **Builds a frame map** containing:
   - Sequential frame number (capture order)
   - VBI frame number (from disc)
   - Phase correctness
   - Pulldown status
   - Quality scores
   - Deletion markers

3. **Applies corrections**:
   - Remove lead-in/out frames
   - Remove invalid frames by phase analysis
   - Correct bad VBI numbers using sequence analysis
   - Remove duplicate frames (keeping best quality)
   - Number pulldown frames
   - Reorder frames by VBI number
   - Pad gaps with black frames
   - Renumber if pulldown present

4. **Outputs**:
   - Reordered TBC file
   - Updated metadata with corrected frame numbers
   - Optional: map-only mode (no output)

### Key Algorithms

**Phase Validation**
```
For each frame:
  if first_field_phase + 1 != second_field_phase:
    mark_for_deletion()
```

**Sequence Analysis Correction**
```
Scan forward 10 frames from each position
Count good/bad frame number sequences
If 2+ good before error and 2+ good after:
  Correct the bad frame number
```

**Duplicate Removal**
```
Find all frames with same VBI number
Keep frame with best quality
Delete others
```

**Pulldown Numbering** (NTSC CAV only)
```
Pulldown frames inherit VBI number from previous frame
Used for sorting, then renumbered sequentially at end
```

---

## Architectural Positioning

### Where Disc Mapping Fits in decode-orc Architecture

Per DESIGN.md Section 13, the architecture already contemplates alignment mapping for multi-source operations. However, **single-source disc mapping is simpler**:

**Multi-Source Alignment** (DESIGN.md Section 13):
- Maps multiple captures to canonical `MasterFieldID` timeline
- Produces `AlignmentMap` artifact
- Used for disc stacking across different physical discs
- Complex: requires cross-source fingerprint matching

**Single-Source Disc Mapping** (This Implementation):
- Maps one capture's fields to corrected sequential order
- Produces field reordering specification
- Used to fix single-disc playback issues (skips, repeats)
- Simpler: uses VBI sequential analysis within one source

### Architectural Classification

Disc mapping analysis is a **Policy/Analysis Tool**, not a core architectural component:

| Category | Is Disc Mapping This? | Reason |
|----------|----------------------|--------|
| Observer | ❌ No | Doesn't analyze individual fields; analyzes entire sequence |
| Decision | ❌ No | Decisions are user overrides; this is automated analysis |
| Stage | ❌ No | Analysis doesn't transform signals; FieldMapStage does that |
| Policy Tool | ✅ Yes | Analyzes data and generates configuration for a stage |

**Comparison to Existing Concepts:**

```
Dropout Detection:
  Observer → generates per-field observations
  Decision → user manually adds/removes dropouts
  Stage → DropoutCorrectStage applies corrections

Disc Mapping:
  Observer → BiphaseObserver, PALPhaseObserver extract VBI data
  Policy → DiscMapperPolicy analyzes sequence, generates mapping
  Stage → FieldMapStage applies reordering
```

The key insight: **Policy tools generate stage parameters, they don't belong in the DAG**.

### Integration Pattern

```
┌─────────────────────────────────────────────────────────────┐
│  Core Architecture (from DESIGN.md)                         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐                                          │
│  │  Observers   │  Extract per-field metadata              │
│  │  (in core)   │  • BiphaseObserver (VBI frame numbers)   │
│  └──────────────┘  • PALPhaseObserver (phase sequences)    │
│                    • FieldParityObserver (parity)          │
│                                                             │
│  ┌──────────────┐                                          │
│  │   Stages     │  Transform signals (appear in DAG)       │
│  │  (in core)   │  • FieldMapStage (apply reordering)      │
│  └──────────────┘                                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│  Policy Tools (Helper Libraries)                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────┐                                      │
│  │ DiscMapperPolicy │  Analyzes observations, generates    │
│  │   (in core/      │  configuration for FieldMapStage     │
│  │   policies/)     │                                      │
│  └──────────────────┘                                      │
│         ↑                                                   │
│         │ Called by                                         │
│         ↓                                                   │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│  User Interfaces                                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────┐              ┌──────────┐                    │
│  │ orc-gui  │              │ orc-cli  │                    │
│  └──────────┘              └──────────┘                    │
│       │                          │                         │
│       │ Both call policy.analyze()                         │
│       │                          │                         │
│       ↓                          ↓                         │
│  Show Dialog              Print to stdout                  │
│  User reviews             User reviews                     │
│       │                          │                         │
│       └──────────┬───────────────┘                         │
│                  ↓                                          │
│       Create FieldMapStage with                            │
│       mapping string parameter                             │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**Data Flow:**
```
1. Source TBC loaded
   ↓
2. Observers run (BiphaseObserver, PALPhaseObserver, etc.)
   ↓ (produces observations)
3. [USER ACTION] Trigger analysis
   ↓
4. DiscMapperPolicy::analyze(source, observations)
   ↓ (analyzes all fields)
5. Returns FieldMappingDecision
   ↓
6. [USER ACTION] Review and accept
   ↓
7. Create FieldMapStage with decision.mapping_spec
   ↓
8. Insert FieldMapStage into DAG
   ↓
9. Execute DAG (FieldMapStage transforms fields)
```

**Clean Separation:**
- **Core** provides: Observers (extract data), Stages (transform data)
- **Policy** provides: Analysis tools (generate configuration)
- **UI** provides: User interaction (trigger, review, apply)

---

## Architectural Mapping to decode-orc

### Component 1: Observers (Already Partially Implemented)

**Existing Observers** (leverage what's there):
- `BiphaseObserver` - Decodes VBI frame numbers ✅
- `FieldParityObserver` - Determines field parity ✅
- `PALPhaseObserver` - Extracts PAL phase ✅

**New Observers Needed**:

1. **`DiscQualityObserver`**
   - Analyzes field quality metrics
   - Outputs: `DiscQualityObservation`
     - `quality_score` (double)
     - Based on dropout count, SNR, signal strength
   - Per-field, stateless

2. **`PulldownObserver`** (NTSC CAV only)
   - Detects pulldown frames using phase pattern analysis
   - Outputs: `PulldownObservation`
     - `is_pulldown` (bool)
     - `confidence` (ConfidenceLevel)
   - Uses observation history (previous 5 fields)

3. **`LeadInOutObserver`**
   - Detects lead-in/lead-out frames
   - Checks for CLV/CAV indicators in VBI
   - Outputs: `LeadInOutObservation`
     - `is_lead_in_out` (bool)
     - `confidence` (ConfidenceLevel)

**Observation Flow**:
```
TBCVideoFieldRepresentation
  ↓ (observers process each field)
BiphaseObservation (VBI frame numbers)
FieldParityObservation (field parity)
PALPhaseObservation (PAL phase)
DiscQualityObservation (quality metrics)
PulldownObservation (pulldown detection)
LeadInOutObservation (lead markers)
```

### Component 2: Disc Mapping Policy (NEW)

**Purpose**: Analyze ALL observations across the entire source to generate a field mapping decision.

**Location**: `orc/core/policies/disc_mapper_policy.h/cpp`

**Architectural Role**: Policy/Analysis Tool
- Lives outside the DAG
- Consumes observer outputs
- Generates stage configuration
- Similar to: hypothetical "optimal filter parameter calculator" or "quality analyzer"

**Key Insight**: This is NOT a stage, NOT an observer, NOT a decision. It's a **policy analysis module** that:
1. Takes complete observation history from all fields
2. Applies disc mapping algorithms
3. Generates a `FieldMappingDecision`

**Interface**:
```cpp
class DiscMapperPolicy {
public:
    struct Options {
        bool delete_unmappable_frames = false;
        bool strict_pulldown_checking = true;
        bool reverse_field_order = false;
    };
    
    // Analyze all observations and generate mapping decision
    FieldMappingDecision analyze(
        const VideoFieldRepresentation& source,
        const Options& options);
    
private:
    void remove_lead_in_out(DiscMap& map);
    void remove_invalid_by_phase(DiscMap& map);
    void correct_vbi_using_sequence_analysis(DiscMap& map);
    void remove_duplicates(DiscMap& map);
    void number_pulldown_frames(DiscMap& map);
    void reorder_and_pad(DiscMap& map);
    
    // Internal frame map structure (similar to legacy)
    struct DiscMap {
        std::vector<FrameInfo> frames;
        VideoFormat format;
        bool is_cav;
        // ... algorithms from legacy discmapper.cpp
    };
};
```

**Decision Output**:
```cpp
struct FieldMappingDecision {
    std::string mapping_spec;  // e.g., "0-10,20-30,11-19,PAD_5,31-100"
    std::vector<FieldMapOperation> operations;
    std::string rationale;  // Human-readable explanation
    bool success;
    std::vector<std::string> warnings;
};

struct FieldMapOperation {
    enum Type { KEEP, DELETE, PAD };
    Type type;
    FieldID field_id;
    FieldID new_position;
    std::string reason;
};
```

**Where Policy Runs**:

Option A: **User-triggered analysis** (Recommended)
```
User clicks "Analyze Disc Mapping" in GUI
  ↓
GUI calls DiscMapperPolicy::analyze()
  ↓
Policy iterates through all fields, reads observations
  ↓
Returns FieldMappingDecision
  ↓
GUI displays decision, user reviews
  ↓
User clicks "Apply Mapping"
  ↓
GUI creates FieldMapStage with mapping_spec parameter
  ↓
DAG updated, preview shows remapped fields
```

Option B: **CLI batch mode**
```
orc-cli --analyze-disc source.tbc
  ↓
Run DiscMapperPolicy::analyze()
  ↓
Print decision to stdout
  ↓
User reviews, optionally saves to .orcprj
```

### Component 3: Field Map Stage (Already Exists!)

**Existing**: `FieldMapStage` ✅

**Current Capability**:
- Takes range specification: `"0-10,20-30,11-19"`
- Remaps field IDs without copying data
- Works perfectly for single-field execution

**Enhancement Needed**: Support padding specification
```
Current:  "0-10,20-30,11-19"
Enhanced: "0-10,PAD_5,20-30,11-19"  // Insert 5 black fields after 0-10
```

**Implementation**:
```cpp
// In FieldMapStage::build_field_mapping()

// Parse "PAD_N" tokens
if (token.starts_with("PAD_")) {
    size_t count = std::stoi(token.substr(4));
    for (size_t i = 0; i < count; ++i) {
        mapping.push_back(FieldID::INVALID);  // Invalid = black field
    }
}

// In FieldMappedRepresentation::get_line()
if (!source_id.is_valid()) {
    return black_line_.data();  // Return cached black line
}
```

---

## Complete Workflow

### GUI Workflow

```
┌─────────────────────────────────────────────────────────┐
│ 1. User loads source.tbc                                │
│    DAG: Source → (preview)                              │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 2. User clicks "Analyze Disc Mapping"                   │
│    GUI runs all observers on source                     │
│    DiscMapperPolicy::analyze(observations)              │
│    → Generates FieldMappingDecision                     │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 3. GUI shows decision dialog:                           │
│    "Found 47 duplicate frames (will remove)"            │
│    "Found 12 gaps (will pad with 143 frames)"           │
│    "3 frames failed phase validation (will remove)"     │
│    [Apply] [Cancel] [Show Details]                      │
└─────────────────────────────────────────────────────────┘
                        ↓ User clicks Apply
┌─────────────────────────────────────────────────────────┐
│ 4. GUI creates FieldMapStage                            │
│    Sets parameter: mapping = "0-34,PAD_5,35-..."        │
│    Inserts into DAG after source                        │
│    DAG: Source → FieldMap → (preview)                   │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 5. Preview updates automatically                        │
│    User can scrub through mapped fields                 │
│    Padded sections show black frames                    │
│    Field numbers reflect new sequence                   │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 6. User adds more stages (dropout correct, etc.)        │
│    DAG: Source → FieldMap → DropoutCorrect → Sink       │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 7. User clicks "Process to Output"                      │
│    Full source execution writes final TBC               │
└─────────────────────────────────────────────────────────┘
```

### CLI Workflow

**Option 1: Two-Step (Analyze, then Apply)**
```bash
# Step 1: Analyze disc (read-only)
orc-cli analyze-disc source.tbc

# Output:
# Disc Mapping Analysis
# =====================
# Format: PAL CAV
# Total frames: 54321
# 
# Operations:
#   - Remove 47 duplicate frames
#   - Remove 3 frames with invalid phase
#   - Pad 12 gaps (143 total pad frames)
#   - Remove 2 lead-in frames
# 
# Mapping: 2-34,PAD_5,35-67,PAD_12,68-54000,...
# 
# Apply with:
#   orc-cli process --field-map "2-34,PAD_5,..." source.tbc output.tbc

# Step 2: Apply mapping (if acceptable)
orc-cli process \
  --field-map "2-34,PAD_5,35-67,PAD_12,68-54000,..." \
  source.tbc output.tbc
```

**Option 2: Non-Interactive Auto-Apply**
```bash
# Analyze and apply automatically (no user review)
orc-cli process --auto-disc-map source.tbc output.tbc

# Internally:
#   1. Runs DiscMapperPolicy::analyze()
#   2. Creates FieldMapStage with result
#   3. Proceeds with full DAG execution
#   4. Writes output

# With additional options:
orc-cli process \
  --auto-disc-map \
  --delete-unmappable \
  --no-strict-pulldown \
  source.tbc output.tbc
```

**Option 3: Save Mapping to Project File**
```bash
# Analyze and save to project file
orc-cli analyze-disc source.tbc --save project.orcprj

# Later, use project file (includes saved mapping)
orc-cli process project.orcprj output.tbc
```

**Option 4: JSON Output for Scripting**
```bash
# Get machine-readable output
orc-cli analyze-disc source.tbc --json > decision.json

# Script can parse JSON and decide whether to proceed
cat decision.json | jq '.mapping'
# Output: "2-34,PAD_5,35-67,PAD_12,68-54000,..."
```

---

## Implementation Plan

### Phase 1: Observer Enhancement (1-2 days)

1. **Implement `DiscQualityObserver`**
   - Calculate quality score from dropout metrics
   - Leverage existing dropout hint infrastructure

2. **Implement `PulldownObserver`**
   - Use observation history
   - NTSC CAV 1-in-5 pattern detection

3. **Implement `LeadInOutObserver`**
   - Check VBI lead-in/out codes
   - CAV frame number 0 detection

4. **Test observers individually**
   - Unit tests with known good/bad TBCs
   - Verify observation output format

### Phase 2: Disc Mapper Policy (3-4 days)

1. **Port core algorithms from `discmapper.cpp`**
   - Extract to `DiscMapperPolicy` class
   - Remove Qt dependencies
   - Adapt to observation-based input

2. **Implement `DiscMap` internal structure**
   - Similar to legacy `Frame` class
   - In-memory working structure

3. **Generate `FieldMappingDecision`**
   - Convert DiscMap to range specification string
   - Include rationale and warnings

4. **Test policy**
   - Unit tests with synthetic observation data
   - Integration tests with real TBCs

### Phase 3: Field Map Stage Enhancement (1 day)

1. **Add padding support**
   - Parse `PAD_N` tokens
   - Generate black fields on-demand
   - Cache single black line (all formats same)

2. **Test padding**
   - Verify black field output
   - Ensure metadata correct

### Phase 4: GUI Integration (2-3 days)

1. **Add "Analyze Disc Mapping" button**
   - Run observers + policy
   - Show results dialog

2. **Decision review dialog**
   - Show operations clearly
   - Allow user to adjust parameters
   - Preview before applying

3. **Auto-create FieldMapStage**
   - Insert into DAG
   - Set parameters from decision

4. **Save mapping in project file**
   - Store decision rationale
   - Allow re-analysis

### Phase 5: CLI Support (1 day)

1. **Add `analyze-disc` command**
   - Instantiate DiscMapperPolicy
   - Run analysis on source TBC
   - Print human-readable output to stdout
   - `--json` flag for machine-readable output
   - `--save` flag to write to .orcprj file
   - Options: `--delete-unmappable`, `--no-strict`, `--reverse`

2. **Add `--auto-disc-map` option to process command**
   - Run analysis automatically
   - Create FieldMapStage with result
   - Add to DAG before processing
   - Pass through policy options

3. **Add `--field-map` option to process command**
   - Accept manual mapping specification
   - Create FieldMapStage with provided string
   - Useful when user has pre-computed mapping

**Implementation Details:**
```cpp
// In orc-cli/main.cpp or process command

// For --auto-disc-map:
if (auto_disc_map) {
    auto source = load_source(input_tbc);
    
    DiscMapperPolicy policy;
    DiscMapperPolicy::Options opts;
    opts.delete_unmappable_frames = delete_unmappable;
    opts.strict_pulldown_checking = !no_strict;
    opts.reverse_field_order = reverse;
    
    auto decision = policy.analyze(*source, opts);
    
    if (!decision.success) {
        std::cerr << "Disc mapping failed: " << decision.rationale << "\n";
        return 1;
    }
    
    // Create FieldMapStage and add to DAG
    auto field_map = std::make_shared<FieldMapStage>();
    field_map->set_parameters({{"ranges", decision.mapping_spec}});
    dag.add_node("disc_map", field_map);
    dag.add_edge(source_node, "disc_map");
}

// For manual --field-map:
if (!field_map_spec.empty()) {
    auto field_map = std::make_shared<FieldMapStage>();
    field_map->set_parameters({{"ranges", field_map_spec}});
    dag.add_node("field_map", field_map);
    dag.add_edge(source_node, "field_map");
}
```

### Phase 6: Testing & Documentation (2 days)

1. **Test with problem discs**
   - Skipping
   - Repeating frames
   - Phase issues
   - Pulldown patterns

2. **Documentation**
   - User guide for GUI workflow
   - CLI examples
   - Algorithm explanation

**Total: ~10-13 days**

---

## Architectural Benefits

### ✅ Maintains Single-Field Execution
- Field Map Stage works per-field
- Preview scrubbing remains fast
- No need to process entire source for preview

### ✅ Separates Concerns
- **Observers** = extract raw data (stateless)
- **Policy** = decide what to do (stateful analysis)
- **Stage** = apply transformation (stateless)

### ✅ Follows DAG Model
- Mapping is a signal transformation stage
- Appears in DAG explicitly
- Can be positioned anywhere in pipeline

### ✅ User Control
- User triggers analysis explicitly
- Reviews decision before applying
- Can adjust parameters
- Can remove/modify mapping later

### ✅ Composable
- FieldMapStage can be used for other purposes
- Policy can generate different strategies
- Observers useful for other analyses

### ✅ Testable
- Each component tested independently
- Policy testable without video data
- Stage testable without analysis

---

## Edge Cases & Considerations

### 1. Partial Mapping
**Issue**: User wants to map only part of source

**Solution**: Policy can accept field range parameter
```cpp
FieldMappingDecision analyze(
    const VideoFieldRepresentation& source,
    FieldIDRange range,  // NEW
    const Options& options);
```

### 2. Manual Override
**Issue**: User disagrees with automatic mapping

**Solution**: 
- GUI allows editing mapping string directly
- Or user can manually build FieldMapStage with custom ranges

### 3. Multiple Sources
**Issue**: Combining mapped sources in stacker

**Solution**: Each source gets its own FieldMapStage
```
Source1 → FieldMap1 ─┐
                      ├→ Stacker → Output
Source2 → FieldMap2 ─┘
```

### 4. Performance
**Issue**: Policy analysis might be slow for large sources

**Solution**:
- Run in background thread (GUI)
- Show progress bar
- Cache decision in project file
- Only re-run if source changes

### 5. CLV vs CAV
**Issue**: Different algorithms for disc types

**Solution**: Policy detects from VBI observations
```cpp
bool is_cav = detect_disc_type(observations);
if (is_cav) {
    // CAV-specific algorithms
} else {
    // CLV-specific algorithms
}
```

### 6. Audio Alignment
**Legacy**: ld-discmap also remaps audio PCM

**Solution**: 
- Phase 1: Video only (TBC + metadata)
- Phase 2: Extend FieldMapStage to handle audio artifacts
- Audio follows video mapping automatically

---

## Alternative Approaches Considered

### ❌ Approach 1: Disc Mapping as a "Meta-Stage"
**Idea**: Create special stage that builds complete map on first field access

**Rejected**: 
- Violates single-field execution principle
- Unpredictable performance (first field slow, rest fast)
- Doesn't fit observer model

### ❌ Approach 2: Automatic Mapping in Source Stage
**Idea**: LDSourceStage detects issues and auto-maps

**Rejected**:
- No user visibility or control
- Can't review decisions
- Hard to debug
- Violates "explicit DAG" principle

### ❌ Approach 3: Mapping as Observer Output
**Idea**: Observer that outputs "this field should be deleted"

**Rejected**:
- Observers don't make decisions
- Observer output is per-field, but mapping requires full-source analysis
- Doesn't fit architectural model

### ✅ Chosen Approach: Three-Component Split
**Why**: 
- Clean separation of concerns
- Fits existing architectural patterns
- User has full visibility and control
- Testable and maintainable
- Composable with other stages

---

## Future Enhancements

### 1. Multi-Pass Analysis
Some discs might need iterative refinement:
```
Pass 1: Coarse mapping (remove obvious errors)
Pass 2: Fine mapping (sequence analysis)
Pass 3: Quality-based refinement
```

### 2. ML-Based Quality
Replace simple quality metrics with ML model:
```cpp
class MLDiscQualityObserver : public Observer {
    // Uses trained model to score field quality
};
```

### 3. Interactive Mapping Editor
GUI tool to manually adjust mapping:
```
Visual timeline showing:
- Original field sequence
- Detected issues
- Proposed mapping
- User can drag/drop to reorder
```

### 4. Mapping Templates
Save/load common mapping patterns:
```
"standard_pulldown_correction.map"
"skip_removal_aggressive.map"
```

---

## Conclusion

The three-component approach (Observers + Policy + Stage) provides a clean, architecturally sound way to port ld-discmap functionality while:

1. **Respecting single-field execution** - Preview remains fast
2. **Maintaining DAG purity** - Mapping is explicit transformation
3. **Providing user control** - Analysis is triggered explicitly, results reviewable
4. **Enabling composition** - Can combine with other stages
5. **Facilitating testing** - Each component testable independently

This approach requires more upfront design than a direct port, but results in a more flexible, maintainable, and user-friendly system that fits decode-orc's architectural vision.

**Recommendation**: Proceed with phased implementation starting with observers, then policy, then GUI integration.
