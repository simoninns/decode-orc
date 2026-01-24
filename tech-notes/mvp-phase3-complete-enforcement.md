# MVP Phase 3: Complete Compiler Enforcement

**Date:** 2026-01-24  
**Status:** Planning - Zero Tolerance for Core Dependencies  
**Objective:** Achieve 100% compiler-enforced MVP architecture with ZERO exceptions

---

## Executive Summary

**Current State (Post Phase 2):**
- ✅ Phases 2.1-2.8 complete (all migrations done)
- ✅ Build system configured (no core in GUI include paths)
- ✅ Compile guards added to core headers
- ✅ Validation script passes
- ❌ **BUILD FAILS** - Compiler enforcement reveals remaining violations
- ❌ Multiple workarounds attempted (relative paths, guard removal, etc.)

**The Problem:**
We've been creating exceptions instead of completing the migration. The compiler is correctly rejecting code that violates MVP architecture, but instead of fixing the violations, we've been weakening the enforcement.

**The Solution:**
Complete the type migration. Move ALL types that GUI uses from core to public API/common. Create presenter methods for ALL operations GUI needs. NO exceptions.

---

## Root Cause Analysis

### Violations Discovered by Compiler

1. **PreviewImage and DropoutRegion**
   - **Where Used**: fieldpreviewwidget.cpp, mainwindow.cpp, render_coordinator.cpp
   - **Current Location**: orc/core/include/preview_renderer.h, orc/core/include/dropout_decision.h
   - **Problem**: GUI implementation files need complete type definitions
   - **Root Cause**: Types defined in core but used by GUI

2. **VideoParameters**
   - **Where Used**: mainwindow.h (Qt signal parameter)
   - **Current Location**: orc/core/include/tbc_metadata.h
   - **Problem**: Qt MOC needs complete type definition for signal parameters
   - **Root Cause**: Type defined in core but exposed in GUI API

3. **VideoSystem and SourceType enum conflicts**
   - **Where Used**: orcgraphicsscene.cpp, guiproject.h
   - **Current Location**: Both orc/common and orc/core (duplicated!)
   - **Problem**: Including core headers brings duplicate definitions
   - **Root Cause**: Types not properly unified between common and core

4. **NodeType and NodeTypeInfo redefinition**
   - **Where Used**: Multiple GUI files
   - **Current Location**: Both orc/common/include/node_type.h and orc/core/include/node_type.h
   - **Problem**: Two separate implementations
   - **Root Cause**: Common has subset, core has full implementation - not unified

---

## Phase 3 Architecture Principles

### 1. **Zero Tolerance**
- NO relative path includes to core
- NO weakening of compile guards
- NO "temporary" exceptions
- NO "documented" violations

### 2. **Type Ownership**
- **Common**: Types used by multiple layers (VideoSystem, SourceType, NodeType, FieldID, NodeID, etc.)
- **Public API**: Types exposed to external consumers (PreviewImage, DropoutRegion, VideoParameters, etc.)
- **Core**: Internal implementation types ONLY (DAG internals, stage implementations, etc.)
- **Presenters**: Wrapper types when needed (can wrap core types but never expose them)

### 3. **Qt MOC Compatibility**
- All types used in Qt signals/slots MUST be in public API or common
- Forward declarations don't work for MOC
- Use pointers/references only when types can't be moved

### 4. **Presenter Completeness**
- Every GUI operation goes through presenters
- GUI never constructs core types directly
- Presenters convert between public API types and core types

---

## Phase 3 Implementation Plan

### Phase 3.1: Unify Common Types (2 days)

**Goal**: Single source of truth for types used across layers

**Tasks:**

1. **Audit Type Duplication**
   ```bash
   # Find types defined in both common and core
   grep -r "enum class VideoSystem" orc/common orc/core
   grep -r "enum class SourceType" orc/common orc/core
   grep -r "enum class NodeType" orc/common orc/core
   grep -r "struct NodeTypeInfo" orc/common orc/core
   ```

2. **Consolidate in Common**
   - Move definitive versions to `orc/common/include/`
   - Core headers include common versions with angle brackets: `#include <node_type.h>`
   - Remove duplicate definitions from core
   - Verify no behavioral changes

3. **Update Core Include Strategy**
   ```cpp
   // orc/core/include/some_core_header.h
   #include <node_type.h>      // From common (angle brackets)
   #include <common_types.h>   // From common
   // NOT: #include "node_type.h"  // Local duplicate
   ```

**Deliverables:**
- ✅ No type redefinition errors
- ✅ Core uses common types consistently
- ✅ Single authoritative source for shared types

---

### Phase 3.2: Move GUI-Visible Types to Public API (3 days)

**Goal**: All types used by GUI live in public API or common

**Types to Move:**

1. **PreviewImage** (PARTIALLY done)
   - Current: `orc::public_api::PreviewImage` exists but missing fields
   - Move from: `orc/core/include/preview_renderer.h` (orc::PreviewImage)
   - Add missing field: `std::vector<DropoutRegion> dropout_regions`
   - Update core to use public API version

2. **DropoutRegion**
   - Current: `orc/core/include/dropout_decision.h`
   - Move to: `orc/public/orc_rendering.h` (with PreviewImage)
   - Simple struct: `{ uint32_t line, start_sample, end_sample; }`

3. **VideoParameters**
   - Current: `orc/core/include/tbc_metadata.h`
   - Move to: `orc/public/orc_video_metadata.h` (new file)
   - Used in Qt signals - must be complete type in public API

4. **PreviewOutputInfo, FrameLineNavigationResult**
   - Already in public API (Phase 2.6) ✅
   - Verify complete and correct

**Migration Pattern:**
```cpp
// Before (core)
namespace orc {
    struct PreviewImage {
        uint32_t width, height;
        std::vector<uint8_t> rgb_data;
        std::vector<DropoutRegion> dropout_regions;  // Missing in public API!
    };
}

// After (public API)
namespace orc {
namespace public_api {
    struct DropoutRegion {
        uint32_t line;
        uint32_t start_sample;
        uint32_t end_sample;
    };
    
    struct PreviewImage {
        uint32_t width, height;
        std::vector<uint8_t> rgb_data;
        std::vector<DropoutRegion> dropout_regions;  // Now complete!
    };
}}

// Core uses public API version
#include <orc_rendering.h>
namespace orc {
    using PreviewImage = orc::public_api::PreviewImage;  // Type alias
}
```

**Deliverables:**
- ✅ All GUI-visible types in public API
- ✅ Core uses public API types internally (via aliases)
- ✅ No type duplication between core and public API

---

### Phase 3.3: Update Presenters to Use Public API Types (2 days)

**Goal**: Presenters expose ONLY public API types, never core types

**Tasks:**

1. **RenderPresenter**
   - Method signatures use `orc::public_api::PreviewImage`
   - Internal conversion from core types to public API types
   - No core types in public methods

2. **ProjectPresenter**
   - Use `orc::public_api::VideoParameters` instead of core version
   - Methods return public API types

3. **AnalysisPresenter**
   - Results use public API types

**Pattern:**
```cpp
// orc/presenters/include/render_presenter.h
#include <orc_rendering.h>  // Public API types

class RenderPresenter {
public:
    // Public methods use public API types
    orc::public_api::PreviewImage renderPreview(
        NodeID node,
        FieldID field,
        PreviewOutputType output_type
    );
    
private:
    // Private methods can use core types
    orc::DAGExecutor executor_;  // OK - private member
    
    // Conversion helpers
    orc::public_api::PreviewImage convertToPublicAPI(
        const orc::PreviewRenderResult& core_result
    );
};
```

**Deliverables:**
- ✅ All presenter public methods use public API types
- ✅ Conversion functions for core ↔ public API
- ✅ GUI can't accidentally access core types through presenters

---

### Phase 3.4: Update GUI to Use Only Public API Types (3 days)

**Goal**: GUI code uses public API types exclusively

**Files to Update:**

1. **fieldpreviewwidget.{h,cpp}**
   ```cpp
   // fieldpreviewwidget.h
   #include <orc_rendering.h>  // Public API
   
   class FieldPreviewWidget {
   public:
       void setImage(const orc::public_api::PreviewImage& image);
   private:
       std::vector<orc::public_api::DropoutRegion> dropout_regions_;
   };
   ```

2. **mainwindow.{h,cpp}**
   ```cpp
   // mainwindow.h
   #include <orc_video_metadata.h>  // Public API
   
   signals:
       void onLineSamplesReady(std::optional<orc::public_api::VideoParameters> params);
   ```

3. **guiproject.{h,cpp}**
   - Already uses presenter types ✅
   - Verify no core types leak through

4. **orcgraphicsscene.cpp**
   - Use common types: `#include <common_types.h>`
   - Use presenter types: `#include "presenters/include/project_presenter.h"`
   - NO core includes

5. **render_coordinator.{h,cpp}**
   - All types from public API or presenters
   - Signals use public API types

**Deliverables:**
- ✅ All GUI headers include ONLY: public API, common, presenters, Qt
- ✅ All GUI implementation files include ONLY: public API, common, presenters, Qt
- ✅ Zero core includes in GUI (verified by compiler)

---

### Phase 3.5: Remove All Workarounds and Exceptions (1 day)

**Goal**: Clean up all temporary hacks

**Tasks:**

1. **Remove Relative Path Includes**
   - Search for `#include "../../core/` in GUI
   - Should be ZERO matches

2. **Restore/Strengthen Compile Guards**
   ```cpp
   // All core headers should have:
   #if defined(ORC_GUI_BUILD)
   #error "GUI code cannot include [this header]. Use [Presenter/PublicAPI] instead."
   #endif
   ```

3. **Remove "Documented Exceptions"**
   - orc-gui-vectorscope: Migrate to use public API types
   - If real-time access needed, create VectorscopePresenter

4. **Update Comments**
   - Remove all "TODO(MVP)" comments
   - Remove "temporary" and "transitional" comments
   - Add "Phase 3 Complete" markers

**Deliverables:**
- ✅ No relative paths to core
- ✅ All compile guards active and strong
- ✅ No documented exceptions
- ✅ Clean, intention-revealing code

---

### Phase 3.6: Build System Final Lockdown (1 day)

**Goal**: Make it impossible to violate MVP architecture

**Tasks:**

1. **GUI CMakeLists.txt - Final State**
   ```cmake
   target_include_directories(orc-gui PRIVATE
       ${CMAKE_SOURCE_DIR}/orc/public           # Public API ONLY
       ${CMAKE_SOURCE_DIR}/orc/presenters/include  # Presenters ONLY
       ${CMAKE_SOURCE_DIR}/orc/common/include   # Common types ONLY
       ${CMAKE_BINARY_DIR}/generated            # Version info
       # ABSOLUTE ZERO CORE ACCESS
   )
   
   target_link_libraries(orc-gui PRIVATE
       orc-public       # Public API
       orc-common       # Common types
       orc-presenters   # Presenter layer (privately links core)
       Qt6::Core Qt6::Widgets QtNodes
       # NO orc-core - not even transitive!
   )
   
   target_compile_definitions(orc-gui PRIVATE
       ORC_GUI_BUILD    # Triggers compile error on core includes
   )
   ```

2. **Vectorscope Library Decision**
   - Option A: Migrate to presenters (recommended)
   - Option B: Create ChromaPresenter with real-time data access
   - Option C: Keep isolated but update to use public API types

3. **CLI Migration (Future Work)**
   - Document that CLI currently has core access
   - Plan for CLI to use presenters in future
   - For now, CLI remains low-level tool

**Deliverables:**
- ✅ GUI has zero core access (build system enforced)
- ✅ Vectorscope properly isolated or migrated
- ✅ CLI access documented as intentional

---

### Phase 3.7: Validation and Testing (2 days)

**Goal**: Prove compiler enforcement works

**Tasks:**

1. **Validation Script Enhancement**
   ```bash
   # validate_mvp.sh already checks:
   # 1. No relative core includes ✅
   # 2. No forbidden headers in GUI headers ✅
   # 3. CMakeLists doesn't expose core ✅
   # 4. Compile guards present ✅
   
   # Add new checks:
   # 5. No core namespace usage in GUI (except via public_api)
   # 6. All public API types have complete definitions
   # 7. No type duplication between layers
   ```

2. **Compiler Test: Intentional Violation**
   ```cpp
   // test_mvp_enforcement.cpp (in orc/gui/)
   #include "project.h"  // Should FAIL with clear error
   
   // Expected error:
   // error: "GUI code cannot include core/include/project.h. 
   //         Use ProjectPresenter instead."
   ```

3. **Full Build Test**
   ```bash
   rm -rf build
   mkdir build && cd build
   cmake ..
   cmake --build . -j$(nproc)
   # MUST succeed with ZERO warnings about MVP violations
   ```

4. **Runtime Testing**
   - All GUI features work
   - No performance regression
   - All analysis tools function
   - Project save/load works

**Deliverables:**
- ✅ Validation script passes
- ✅ Intentional violations caught at compile time
- ✅ Full build succeeds
- ✅ All features work

---

## Success Criteria (100% Enforcement)

### Build-Time Enforcement
- [ ] **Zero** core header includes in GUI source files
- [ ] **Zero** core directories in GUI CMakeLists.txt include paths
- [ ] **Zero** direct orc-core links in GUI CMakeLists.txt
- [ ] Compile guards trigger on ANY attempt to include core from GUI
- [ ] Build succeeds with zero MVP-related warnings

### Type Architecture
- [ ] All types used by GUI are in public API or common
- [ ] No type duplication between layers
- [ ] Core can use public API types (one-way dependency)
- [ ] Presenters convert between core and public API types

### Code Quality
- [ ] No relative paths to core anywhere in GUI
- [ ] No "TODO(MVP)" comments remaining
- [ ] No "temporary" or "transitional" workarounds
- [ ] Zero documented exceptions (except CLI if kept)

### Validation
- [ ] `validate_mvp.sh` passes 100%
- [ ] Intentional violations caught by compiler
- [ ] Full clean build succeeds
- [ ] All features functionally tested

---

## Timeline

**Total Estimated Time:** 2 weeks

| Phase | Duration | Dependencies |
|-------|----------|--------------|
| 3.1: Unify Common Types | 2 days | None |
| 3.2: Move Types to Public API | 3 days | 3.1 |
| 3.3: Update Presenters | 2 days | 3.2 |
| 3.4: Update GUI | 3 days | 3.3 |
| 3.5: Remove Workarounds | 1 day | 3.4 |
| 3.6: Build Lockdown | 1 day | 3.5 |
| 3.7: Validation | 2 days | 3.6 |

**Milestones:**
- End of Week 1: Types migrated, presenters updated (3.1-3.3)
- End of Week 2: GUI updated, build succeeds, 100% validated (3.4-3.7)

---

## Risk Mitigation

### Risk: Breaking Existing Functionality
**Mitigation:**
- Comprehensive automated tests before starting
- Migrate one type at a time
- Validate after each type migration
- Keep old code in git branches

### Risk: Qt MOC Compatibility Issues
**Mitigation:**
- Test MOC generation after each type move
- Use forward declarations where possible
- Complete types in public API when required for MOC

### Risk: Performance Impact
**Mitigation:**
- Profile before and after
- Type conversions should be trivial (memcpy or move)
- Most conversions happen at UI boundaries (low frequency)
- Inline small conversion functions

### Risk: Missed Types
**Mitigation:**
- Compiler will catch them (that's the point!)
- Systematic grep for all type usage in GUI
- Build frequently during migration

---

## Anti-Patterns to Avoid

### ❌ DON'T: Use Relative Paths
```cpp
#include "../../core/include/project.h"  // WRONG
```

### ❌ DON'T: Weaken Compile Guards
```cpp
#if defined(ORC_GUI_BUILD) && !defined(ALLOW_CORE_IN_CPP)  // WRONG - creating exceptions
```

### ❌ DON'T: Create "Documented Exceptions"
```cmake
# WRONG - exceptions defeat the purpose
target_include_directories(orc-gui-special-case PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/core/include  # "Needed for X"
)
```

### ❌ DON'T: Duplicate Types
```cpp
// orc/common/common_types.h
enum class VideoSystem { NTSC, PAL };

// orc/core/tbc_metadata.h
enum class VideoSystem { NTSC, PAL };  // WRONG - redefinition
```

### ✅ DO: Move Types to Shared Location
```cpp
// orc/common/common_types.h (single source of truth)
enum class VideoSystem { NTSC, PAL, Unknown };

// orc/core/whatever.h
#include <common_types.h>  // Use from common
using orc::VideoSystem;    // Bring into scope
```

### ✅ DO: Use Public API in Presenters
```cpp
// orc/presenters/include/render_presenter.h
#include <orc_rendering.h>  // Public API

class RenderPresenter {
    orc::public_api::PreviewImage render(...);  // Public API type
};
```

### ✅ DO: Convert at Presenter Boundary
```cpp
// orc/presenters/src/render_presenter.cpp
#include "preview_renderer.h"  // Core (private to presenter)

PreviewImage RenderPresenter::render(...) {
    auto core_result = core_renderer_.render(...);
    return convertToPublicAPI(core_result);  // Convert at boundary
}
```

---

## Completion Checklist

### Phase 3.1: Common Types
- [ ] Audit complete: list all duplicate types
- [ ] VideoSystem unified in common
- [ ] SourceType unified in common
- [ ] NodeType unified in common  
- [ ] Core includes common types with angle brackets
- [ ] No redefinition errors in build

### Phase 3.2: Public API Types
- [ ] DropoutRegion moved to public API
- [ ] PreviewImage complete in public API (with dropout_regions)
- [ ] VideoParameters moved to public API
- [ ] Core uses public API types (via aliases)
- [ ] No duplicate type definitions

### Phase 3.3: Presenter Updates
- [ ] RenderPresenter uses public API types in public methods
- [ ] ProjectPresenter uses public API types in public methods
- [ ] AnalysisPresenter uses public API types in public methods
- [ ] Conversion functions implemented
- [ ] Presenters build successfully

### Phase 3.4: GUI Updates
- [ ] fieldpreviewwidget uses public API types
- [ ] mainwindow uses public API types
- [ ] guiproject uses presenter types only
- [ ] orcgraphicsscene uses common/presenter types
- [ ] render_coordinator uses public API types
- [ ] All GUI files include only: public API, common, presenters

### Phase 3.5: Cleanup
- [ ] Zero relative paths to core in GUI
- [ ] All compile guards restored and active
- [ ] orc-gui-vectorscope migrated or documented
- [ ] All TODO(MVP) comments removed
- [ ] Code is clean and intention-revealing

### Phase 3.6: Build Lockdown
- [ ] GUI CMakeLists has zero core paths
- [ ] GUI CMakeLists has zero orc-core link
- [ ] Compile definitions include ORC_GUI_BUILD
- [ ] Vectorscope properly handled

### Phase 3.7: Validation
- [ ] validate_mvp.sh enhanced with new checks
- [ ] validate_mvp.sh passes 100%
- [ ] Intentional violation test added and fails correctly
- [ ] Full clean build succeeds
- [ ] All GUI features tested and working
- [ ] No performance regression

---

## Next Steps

1. **Review this plan** with team
2. **Create Phase 3.1 branch** for common type unification
3. **Begin systematic migration** following this plan
4. **No shortcuts or exceptions** - complete each phase fully
5. **Celebrate** when compiler enforcement is 100% active! 🎉

---

**Remember**: The goal is not to work around the compiler, but to satisfy it by having a truly clean architecture. Every exception weakens the enforcement. Zero exceptions means zero ways to accidentally break MVP architecture.

**Document Owner:** Development Team  
**Related Documents:**
- [mvp-phase2-completion-plan.md](mvp-phase2-completion-plan.md) - Phase 2 work (complete)
- [mvp-architecture-implementation-plan.md](mvp-architecture-implementation-plan.md) - Original plan
