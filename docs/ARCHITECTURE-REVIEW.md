# ORC-Core Architecture & Code Quality Review

**Review Date:** 28 December 2025  
**Scope:** orc/core module  
**Focus:** Architectural issues, code duplication, stubs, orphaned code, and style consistency

---

## Executive Summary

The orc-core codebase is generally well-structured with good separation of concerns, consistent use of modern C++ patterns, and clean modularization. However, after extensive refactoring, several areas need attention:

- **3 areas of significant code duplication** requiring refactoring
- **1 critical incomplete feature** (Stacker stage)
- **Header organization inconsistencies** between `include/` and `observers/`
- **21+ TODO comments** requiring triage and prioritization
- **Several legacy decoder "magic numbers"** needing documentation

---

## 1. Code Duplication Issues

### 1.1 Analysis Tool Boilerplate (HIGH PRIORITY)

**Files Affected:**
- `orc/core/analysis/dropout/dropout_analysis.cpp`
- `orc/core/analysis/snr/snr_analysis.cpp`
- `orc/core/analysis/burst_level/burst_level_analysis.cpp`

**Issue:**
These three analysis tools are nearly identical, differing only in:
- Tool ID/name strings
- Comments mentioning the specific analysis type

**Duplicated Pattern:**
```cpp
AnalysisResult XxxAnalysisTool::analyze(const AnalysisContext& ctx, AnalysisProgress* progress) {
    (void)ctx;  // Unused - GUI-triggered tool
    AnalysisResult result;
    
    // This is a batch analysis tool that will be triggered via the GUI
    // The actual data processing happens in the RenderCoordinator and XxxAnalysisDecoder
    
    if (progress) {
        progress->setStatus("Xxx analysis will be processed via GUI");
        progress->setProgress(100);
    }
    
    result.status = AnalysisResult::Success;
    result.summary = "Xxx analysis tool registered";
    return result;
}

bool XxxAnalysisTool::canApplyToGraph() const {
    // Analysis only, nothing to apply back to graph
    return false;
}

bool XxxAnalysisTool::applyToGraph(...) {
    (void)result; (void)project; (void)node_id;
    // Analysis only, nothing to apply
    return false;
}
```

**Recommendation:**
Create a base class `BatchAnalysisTool` that implements the common pattern:

```cpp
// New file: orc/core/analysis/batch_analysis_tool.h
class BatchAnalysisTool : public AnalysisTool {
public:
    AnalysisResult analyze(const AnalysisContext& ctx, 
                          AnalysisProgress* progress) override final;
    bool canApplyToGraph() const override final { return false; }
    bool applyToGraph(const AnalysisResult&, Project&, 
                     const std::string&) override final { return false; }
    bool isApplicableToStage(const std::string& stage_name) const override {
        return stage_name != "chroma_sink";
    }
    int estimateDurationSeconds(const AnalysisContext&) const override { 
        return -1; 
    }

protected:
    virtual std::string tool_id() const = 0;
    virtual std::string tool_name_display() const = 0;
    virtual std::string decoder_name() const = 0;  // For logging
};
```

**Impact:** Eliminates ~240 lines of duplicated code across 3 files.

---

## 2. Incomplete Implementations & Stubs

### 2.1 Stacker Stage (CRITICAL - HIGH PRIORITY)

**File:** `orc/core/stages/stacker/stacker_stage.cpp`

**Current State:** 
The stage is a placeholder that simply returns the first source unchanged, despite having full UI integration and appearing as a functional feature.

**TODO Comments Found:**
```
Line  90: TODO: Implement full stacking logic based on legacy ld-disc-stacker
Line 106: TODO: Implement actual stacking algorithm
Line 122: TODO: Implement dropout region tracking
Line 212: TODO: Add to output_dropouts
Line 240: TODO: Implement neighbor modes (3, 4)
```

**Code Evidence:**
```cpp
// Line 106-108
// TODO: Implement actual stacking algorithm
// This is a placeholder that returns the first source
// Full implementation would:
// 1. For each field in range
// 2. Get field data from all sources
// 3. Apply stacking mode pixel-by-pixel
// 4. Handle dropouts appropriately
// 5. Create new VideoFieldRepresentation with stacked data

return sources[0];  // Just returns first source!
```

**Impact:** 
- Users believe they have a working multi-source stacking feature
- The README.md in the stacker folder documents full functionality
- Only passthrough mode actually works

**Recommendations:**
1. **Short-term:** Add prominent UI warning that stage is experimental/incomplete
2. **Medium-term:** Implement the core stacking algorithm from legacy ld-disc-stacker
3. **Alternative:** If not planning to complete, remove from UI or mark as "Developer Preview"

**Reference:** See `orc/core/stages/stacker/README.md` for full expected functionality

---

### 2.2 Other Incomplete Features

**TBC Metadata Observation Storage** (`tbc_video_field_representation.cpp:381`)
```cpp
// TODO: Implement reading observations from TBC metadata database
// For now, return empty - observers will compute fresh from TBC signal data
return {};
```
**Impact:** Medium - Prevents caching of expensive observation computations
**Recommendation:** Implement for performance optimization in v2.0

**Dropout Correction** (`dropout_correct_stage.cpp:194`)
```cpp
// TODO: Support explicit dropout list and decisions
```
**Impact:** Medium - Limits flexibility of dropout handling
**Recommendation:** Plan for future enhancement

**CLV Timecode Parsing** (`biphase_observer.cpp:201`)
```cpp
// TODO: Full CLV timecode parsing
```
**Impact:** Low - CAV discs work fine, CLV is less common
**Recommendation:** Add to backlog

---

## 3. Header Organization Inconsistency

### 3.1 Observer Headers in Two Locations

**Issue:** Observer headers are split between two directories without clear rationale.

**Headers in `include/` directory (5 files):**
- `include/dropout_analysis_observer.h`
- `include/snr_analysis_observer.h`
- `include/lead_in_out_observer.h`
- `include/pulldown_observer.h`
- `include/disc_quality_observer.h`

**Headers in `observers/` directory (11 files):**
- `observers/biphase_observer.h`
- `observers/vitc_observer.h`
- `observers/video_id_observer.h`
- `observers/white_flag_observer.h`
- `observers/vits_observer.h`
- `observers/burst_level_observer.h`
- `observers/fm_code_observer.h`
- `observers/closed_caption_observer.h`
- `observers/observer.h` (base class)
- `observers/observation_history.h`
- Plus `observers/dropout_analysis_observer.h` (DUPLICATE!)

**Recommendation:**

**Option A (Recommended):** Consolidate to `observers/` directory
- Move the 5 headers from `include/` to `observers/`
- Update `CMakeLists.txt` include paths if needed
- Rationale: Observers are a cohesive subsystem, keeping them together improves maintainability

**Option B:** Consolidate to `include/` directory
- Move all observer headers to `include/`
- Rationale: Only if observers are truly part of the public API

**Current CMakeLists.txt includes both:**
```cmake
target_include_directories(orc-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/observers
    # ... other paths
)
```

---

## 4. Orphaned Code & Comments

### 4.1 Removed Observer References

**File:** `orc/core/CMakeLists.txt` (Lines 35-36)

```cmake
# observers/field_parity_observer.cpp  # Removed: field parity comes from hints only
# observers/pal_phase_observer.cpp     # Removed: PAL phase comes from hints only
```

**Issue:** These observers were removed and replaced with the hint system, but commented-out references remain in build files.

**Recommendation:**
- **Option A:** Remove comments entirely (clean slate)
- **Option B:** Add architectural notes to separate documentation explaining the design decision to use hints instead of observers

**Related Design Decision:**
The system now uses `FieldParityHint` and `FieldPhaseHint` structures instead of observers for these properties. This is a good architectural decision (hints are faster and more reliable than computed observations), but should be documented in `docs/DESIGN.md` or similar.

---

## 5. Stage Registration Mechanism

### 5.1 Force-Linking Workaround

**File:** `orc/core/stage_init.cpp`

**Current Implementation:**
```cpp
void force_stage_linking() {
    // Create dummy shared_ptr to force vtable instantiation
    // This ensures the object files are linked
    [[maybe_unused]] auto dummy1 = std::make_shared<LDPALSourceStage>();
    [[maybe_unused]] auto dummy2 = std::make_shared<LDNTSCSourceStage>();
    [[maybe_unused]] auto dummy3 = std::make_shared<DropoutCorrectStage>();
    [[maybe_unused]] auto dummy4 = std::make_shared<FieldInvertStage>();
    [[maybe_unused]] auto dummy5 = std::make_shared<FieldMapStage>();
    [[maybe_unused]] auto dummy6 = std::make_shared<LDSinkStage>();
    [[maybe_unused]] auto dummy7 = std::make_shared<StackerStage>();
    [[maybe_unused]] auto dummy8 = std::make_shared<ChromaSinkStage>();
}
```

**Issue:** 
This is a workaround for C++ static initialization and linker behavior. It works but:
- Fragile - easy to forget to add new stages
- Not self-documenting
- Relies on side effects of constructor execution

**Current Side Effect:** Each stage constructor calls `StageRegistry::register_stage()` to add itself to the global registry.

**Alternative Approaches:**

**Option 1: Explicit Registration Macro**
```cpp
// In each stage .cpp file
REGISTER_STAGE(LDPALSourceStage);

// Expands to:
namespace { 
    static StageRegistration<LDPALSourceStage> _reg; 
}
```

**Option 2: Centralized Registration**
```cpp
void initialize_stages() {
    StageRegistry::register_stage(std::make_shared<LDPALSourceStage>());
    StageRegistry::register_stage(std::make_shared<LDNTSCSourceStage>());
    // ... explicit list
}
```

**Option 3: Keep Current (Document It)**
- Add comments explaining why this pattern is needed
- Document in `docs/DESIGN.md` under "Stage Registration Architecture"

**Recommendation:** Option 3 for now (document), consider Option 1 for v2.0 refactor.

---

## 6. Legacy Decoder Magic Numbers

### 6.1 PAL Color Decoder Unexplained Constants

**File:** `orc/core/stages/chroma_sink/decoders/palcolour.cpp`

**Issues Found:**

```cpp
// Line 135-137
// HACK - For whatever reason Pal-M ends up with the vectors swapped and out of phase
// with how the standard is documented.
// TODO: Find a proper solution to this.
```

```cpp
// Line 170
// XXX where does the 0.5* come from?
fir_coeffs[filterSignalCount] = 0.5 * cos(f);
```

```cpp
// Line 364
// XXX magic number 130000 !!! check!
if (absSignal > 130000) {
```

**File:** `orc/core/stages/chroma_sink/decoders/transformpal3d.cpp`

```cpp
// Line 246
// XXX Why ZTILE / 4? It should be (6 * ZTILE) / 8...
const size_t thresholdsSize = (ZTILE / 4) * XTILE * YTILE;
```

**File:** `orc/core/stages/chroma_sink/decoders/comb.cpp`

```cpp
// Line 591
// TODO: Needed to shift the chroma 1 sample to the right to get it to line up
// with the luma. This is a bit of a hack and needs to be investigated.
```

**Issue:** These appear to be ported from legacy ld-decode tools but lack documentation explaining the rationale.

**Recommendations:**
1. Research each constant against original ld-decode source and PAL/NTSC specifications
2. Add detailed comments explaining the engineering rationale
3. Consider creating named constants:
   ```cpp
   // PAL-M has inverted color burst compared to PAL-B/G due to different
   // color subcarrier phase conventions. This compensates for the phase inversion.
   constexpr double PAL_M_PHASE_CORRECTION = -1.0;
   ```
4. Create tracking issues for each "XXX" to investigate proper solutions

**Impact:** Low priority - decoders work in practice, but lack of documentation makes future maintenance difficult.

---

## 7. TODO Inventory & Triage

### 7.1 Complete TODO List

| Priority | File | Line | Description | Recommendation |
|----------|------|------|-------------|----------------|
| **HIGH** | `stacker_stage.cpp` | 90, 106 | Implement full stacking logic | Complete or mark experimental |
| **HIGH** | `stacker_stage.cpp` | 122, 212 | Dropout region tracking | Required for stacker completion |
| **MEDIUM** | `tbc_video_field_representation.cpp` | 381 | TBC metadata observation storage | Performance optimization |
| **MEDIUM** | `dropout_correct_stage.cpp` | 194, 229 | Explicit dropout support, get params from video | Feature enhancement |
| **LOW** | `biphase_observer.cpp` | 201 | Full CLV timecode parsing | Nice to have |
| **LOW** | `preview_renderer.cpp` | 354, 805 | Future output types, IRE scaling | Enhancement |
| **LOW** | `lead_in_out_observer.cpp` | 99 | Add fields to BiphaseObservation | Data structure expansion |
| **LOW** | `pulldown_observer.cpp` | 109 | Full phase pattern analysis | Advanced feature |
| **LOW** | `comb.cpp` | 591 | Chroma alignment investigation | Decoder refinement |

### 7.2 Recommended TODO Categorization

**Tag TODOs with issue tracking:**
```cpp
// TODO(#123): Implement full stacking logic - High Priority
// TODO(v2.0): Add full CLV timecode parsing - Low Priority
// TODO(perf): Cache observations in TBC metadata - Performance
// TODO(legacy): Investigate PAL-M phase hack - Technical Debt
```

---

## 8. Positive Observations

### 8.1 Well-Executed Patterns

✅ **Consistent Namespace Usage:** All code properly namespaced under `orc::`

✅ **Modern C++ Practices:**
- Smart pointers throughout (no raw `new`/`delete`)
- RAII for resource management
- `std::optional` for nullable values
- Move semantics where appropriate

✅ **Clean Separation of Concerns:**
- Stages (processing nodes)
- Observers (data extraction)
- Analysis tools (batch processing)
- Clear layering

✅ **Excellent Logging Infrastructure:**
- Consistent use of `ORC_LOG_DEBUG/INFO/ERROR`
- Meaningful context in log messages
- Debug-level logging properly scoped

✅ **Documentation Quality:**
- READMEs in complex subsystems (e.g., `stages/stacker/README.md`)
- Header comments explain purpose
- Comments focus on "why" not just "what"

✅ **Build System:**
- Clean CMake organization
- Proper dependency management
- Modular library structure

### 8.2 Intentionally Unused Parameters

Many observers have `(void)history; // Unused` - this is **not a problem**. The observer interface includes `ObservationHistory` for future extensibility, but many observers don't currently need historical context. This is good forward-thinking design.

---

## 9. Action Plan

### 9.1 Immediate Actions (Before Next Release)

1. **Refactor Analysis Tool Duplication**
   - Create `BatchAnalysisTool` base class
   - Migrate 3 analysis tools to inherit from it
   - Estimated effort: 2-4 hours

2. **Address Stacker Stage Status**
   - Option A: Complete implementation (large effort)
   - Option B: Add UI warning "Experimental - Development Only"
   - Option C: Hide from production UI until complete
   - Decision needed from product owner

3. **Consolidate Observer Headers**
   - Move all observer headers to `observers/` directory
   - Remove duplicates
   - Estimated effort: 1 hour

### 9.2 Short-Term Actions (Next Sprint)

4. **Clean Up Orphaned Comments**
   - Remove commented-out observer references in CMakeLists.txt
   - Estimated effort: 15 minutes

5. **Document Stage Registration**
   - Add section to `docs/DESIGN.md` explaining force-linking pattern
   - Estimated effort: 30 minutes

6. **Triage TODO Comments**
   - Add tracking tags to all TODOs
   - Create GitHub issues for HIGH/MEDIUM items
   - Estimated effort: 1 hour

### 9.3 Long-Term Actions (Future Versions)

7. **Complete Stacker Implementation**
   - Port algorithm from legacy ld-disc-stacker
   - Add comprehensive tests
   - Estimated effort: 2-3 weeks

8. **Document Legacy Decoder Constants**
   - Research each "XXX" and "magic number"
   - Add detailed comments with references
   - Consider refactoring to named constants
   - Estimated effort: 1 week

9. **Implement Observation Caching**
   - Add TBC metadata observation storage
   - Performance optimization for large files
   - Estimated effort: 1 week

---

## 10. Metrics

**Codebase Statistics:**
- Total source files in orc-core: ~144 (.cpp and .h files)
- Headers in `include/`: 32 files
- Headers in `observers/`: 11 files
- TODO comments: 21+ instances
- Code duplication instances: 3 major (analysis tools)

**Code Quality Indicators:**
- ✅ No raw pointers in business logic
- ✅ Consistent coding style
- ✅ All classes in `orc` namespace
- ✅ Modern C++17 features used appropriately
- ⚠️ Some legacy C code in chroma decoders (expected)
- ⚠️ One critical incomplete feature (Stacker)

---

## 11. Conclusion

The orc-core codebase demonstrates solid architecture and good engineering practices. The main issues are artifacts of extensive refactoring rather than fundamental design problems. The recommended actions are straightforward and will significantly improve code maintainability without requiring major architectural changes.

**Priority Focus:**
1. Resolve Stacker stage status (critical for user trust)
2. Eliminate analysis tool duplication (technical debt)
3. Consolidate header organization (maintainability)

The codebase is production-ready with the exception of the Stacker stage, which should either be completed or clearly marked as experimental.
