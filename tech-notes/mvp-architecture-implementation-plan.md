# MVP Architecture Implementation Plan

**Date:** 2026-01-22  
**Status:** Draft - Ready for Review  
**Related:** [mvp-architecture-violations.md](mvp-architecture-violations.md)

## Executive Summary

This document provides a complete implementation plan to establish and enforce proper MVP (Model-View-Presenter) architecture across the decode-orc codebase. The plan addresses the 64+ architecture violations identified in the current codebase and establishes both build-time and compile-time enforcement mechanisms.

**Key Objectives:**
1. Eliminate all direct core header includes from GUI/CLI layers
2. Establish clean public API as the sole interface to core functionality
3. Enforce architecture through build system configuration
4. Enable compile-time violation detection
5. Extract business logic from presentation layers
6. Create testable, maintainable layer boundaries

**Estimated Effort:** 3-4 weeks full-time development  
**Risk Level:** Medium - requires careful coordination but clear path forward

---

## Table of Contents

1. [Architectural Principles](#architectural-principles)
2. [Layer Definitions](#layer-definitions)
3. [Implementation Phases](#implementation-phases)
4. [Build System Enforcement](#build-system-enforcement)
5. [Compiler Enforcement](#compiler-enforcement)
6. [Migration Strategy](#migration-strategy)
7. [Validation & Testing](#validation--testing)
8. [Risk Mitigation](#risk-mitigation)
9. [Success Criteria](#success-criteria)

---

## Architectural Principles

### Core Principles

1. **Dependency Direction**: Dependencies flow inward only
   ```
   GUI/CLI → Public API → Core Implementation
   (Never: Core → Public API or Public API → GUI/CLI)
   ```

2. **Interface Segregation**: Each layer exposes minimal necessary interface
   - GUI sees only public API headers
   - CLI sees only public API headers  
   - Core sees only its own internal headers

3. **Type Unification**: Single source of truth for all type definitions
   - No duplicate type definitions across layers
   - Shared types live in common location accessible to all layers
   - Public API uses shared types, doesn't redefine them

4. **Business Logic Location**: Business logic lives in core only
   - GUI/CLI are pure presentation - no decision-making
   - All domain logic, algorithms, validation in core
   - Presenters coordinate but don't implement logic

### Layer Responsibilities

**Core Layer:**
- All business logic and domain models
- Data processing pipelines
- File I/O and persistence
- Algorithm implementations
- Type definitions (via shared common module)

**Public API Layer:**
- Clean C interface to core functionality
- Resource lifetime management (create/destroy functions)
- Error handling and status codes
- Opaque handle types where appropriate
- No business logic - pure delegation to core

**Presenter Layer (New):**
- Coordinates between view and model
- Translates view events to core operations
- Translates core state to view updates
- No Qt dependencies
- No business logic

**GUI Layer:**
- Qt widgets and UI components only
- User interaction handling
- Visual rendering and layout
- Delegates all logic to presenter
- Only includes public API headers

**CLI Layer:**
- Command-line parsing
- Text output formatting
- Progress reporting
- Delegates all logic to public API
- Only includes public API headers

---

## Layer Definitions

### Directory Structure

```
orc/
├── common/                  # Shared types and interfaces
│   ├── include/
│   │   ├── field_id.h      # Common type definitions
│   │   ├── node_id.h       
│   │   ├── node_type.h
│   │   ├── error_codes.h
│   │   └── common_types.h
│   └── CMakeLists.txt
│
├── core/                    # Core implementation (private)
│   ├── include/            # Core-internal headers
│   ├── stages/             # Processing stages
│   ├── analysis/           # Analysis implementations
│   ├── util/               # Internal utilities
│   └── CMakeLists.txt
│
├── public/                  # Public API (facade)
│   ├── orc_api.h           # Main API header
│   ├── orc_types.h         # Type aliases to common/
│   ├── orc_project.h       # Project management
│   ├── orc_rendering.h     # Rendering interface
│   ├── orc_analysis.h      # Analysis interface
│   ├── orc_stages.h        # Stage registry
│   ├── orc_metadata.h      # Metadata access
│   ├── orc_logging.h       # Logging interface
│   ├── impl/               # Implementation bridge
│   │   └── public_api_impl.cpp
│   └── CMakeLists.txt
│
├── presenters/             # Presenter layer (NEW)
│   ├── include/
│   │   ├── project_presenter.h
│   │   ├── render_presenter.h
│   │   ├── analysis_presenter.h
│   │   └── hints_presenter.h
│   ├── src/
│   │   ├── project_presenter.cpp
│   │   ├── render_presenter.cpp
│   │   └── ...
│   └── CMakeLists.txt
│
├── gui/                     # GUI views only
│   ├── mainwindow.h        # Pure view - no logic
│   ├── mainwindow.cpp
│   └── CMakeLists.txt
│
└── cli/                     # CLI interface
    ├── main.cpp
    └── CMakeLists.txt
```

---

## Implementation Phases

### Phase 0: Preparation (2 days)

**Goal:** Set up infrastructure and tooling

**Tasks:**

1. **Create `orc/common/` module**
   ```cmake
   # orc/common/CMakeLists.txt
   add_library(orc-common INTERFACE)
   target_include_directories(orc-common INTERFACE
       $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
       $<INSTALL_INTERFACE:include>
   )
   ```

2. **Move shared types to common/**
   - Move `field_id.h` from `core/include/` → `common/include/`
   - Move `node_id.h` from `core/include/` → `common/include/`  
   - Move `node_type.h` from `core/include/` → `common/include/`
   - Create `common/include/error_codes.h` for all error enums
   - Create `common/include/common_types.h` for shared primitives

3. **Update core to use common types**
   - Update all core headers to include from `<field_id.h>` not `"field_id.h"`
   - Link core library against `orc-common`
   - Verify core builds successfully

4. **Create validation tooling**
   - Enhance `orc/public/validate_mvp.sh`
   - Add CI integration script
   - Create pre-commit hook template

**Deliverables:**
- ✅ `orc/common/` module compiles
- ✅ Core builds with common types
- ✅ Validation script functional
- ✅ No type definition conflicts

---

### Phase 1: Public API Type Unification (3 days)

**Goal:** Eliminate type conflicts between public API and core

**Tasks:**

1. **Rewrite `orc/public/orc_types.h`**
   - Remove all struct definitions
   - Include common headers instead
   - Create type aliases where needed:
     ```cpp
     // orc/public/orc_types.h
     #ifndef ORC_PUBLIC_TYPES_H
     #define ORC_PUBLIC_TYPES_H
     
     #include <field_id.h>
     #include <node_id.h>
     #include <node_type.h>
     #include <error_codes.h>
     
     // Opaque handles for GUI/CLI
     typedef struct OrcProject* OrcProjectHandle;
     typedef struct OrcRenderer* OrcRendererHandle;
     typedef struct OrcAnalysisContext* OrcAnalysisHandle;
     
     #endif
     ```

2. **Update public API headers**
   - Remove duplicate type definitions from all `orc_*.h` files
   - Use common types directly
   - Define only opaque handles and function signatures

3. **Create `orc/public/impl/type_bridge.h`** (private to impl)
   - Conversion helpers between handles and core objects
   - Internal only - not exposed to API users
     ```cpp
     // orc/public/impl/type_bridge.h
     #pragma once
     #include <orc_types.h>
     #include "../core/include/project.h"
     
     namespace orc::public_api {
         inline orc::Project* toCore(OrcProjectHandle h) {
             return reinterpret_cast<orc::Project*>(h);
         }
         inline OrcProjectHandle toHandle(orc::Project* p) {
             return reinterpret_cast<OrcProjectHandle>(p);
         }
     }
     ```

4. **Update `public_api_impl.cpp`**
   - Include both public and core headers safely
   - Use type bridge for conversions
   - Implement all function stubs

**Deliverables:**
- ✅ No type definition conflicts
- ✅ Public API compiles cleanly
- ✅ `public_api_impl.cpp` compiles
- ✅ Type bridge tested

**Validation:**
```bash
# Must compile without errors
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make orc-public
```

---

### Phase 2: Presenter Layer Creation (5 days)

**Goal:** Extract business logic from GUI into reusable presenter layer

**Tasks:**

1. **Create presenter module structure**
   ```cmake
   # orc/presenters/CMakeLists.txt
   add_library(orc-presenters STATIC
       src/project_presenter.cpp
       src/render_presenter.cpp
       src/analysis_presenter.cpp
       src/hints_presenter.cpp
       src/dropout_presenter.cpp
   )
   
   target_link_libraries(orc-presenters
       PUBLIC orc-public
       PRIVATE orc-core
   )
   
   # CRITICAL: Presenters can see both public and core
   target_include_directories(orc-presenters PRIVATE
       ${CMAKE_SOURCE_DIR}/orc/core/include
       ${CMAKE_SOURCE_DIR}/orc/public
   )
   ```

2. **Implement ProjectPresenter**
   - Extract `MainWindow::quickProject()` logic
   - Create template system for project creation
   - Provide methods for project lifecycle:
     ```cpp
     class ProjectPresenter {
     public:
         static OrcProjectHandle createQuickProject(
             VideoFormat format,
             const std::vector<std::string>& inputFiles
         );
         
         static std::vector<NodeInfo> getAvailableStages(
             OrcProjectHandle project,
             VideoFormat format
         );
         
         static bool validateProject(OrcProjectHandle project);
     };
     ```

3. **Implement RenderPresenter**
   - Extract rendering coordination from `RenderCoordinator`
   - Provide clean interface for preview/export
     ```cpp
     class RenderPresenter {
     public:
         RenderPresenter(OrcProjectHandle project);
         
         PreviewImage renderPreview(NodeID node, FieldID field);
         void exportSequence(NodeID node, const ExportOptions& opts);
         RenderProgress getProgress() const;
     };
     ```

4. **Implement AnalysisPresenter**
   - Consolidate analysis data access
   - Decouple from Qt types
     ```cpp
     class AnalysisPresenter {
     public:
         DropoutAnalysisData getDropoutAnalysis(NodeID node);
         SNRAnalysisData getSNRAnalysis(NodeID node);
         BurstAnalysisData getBurstAnalysis(NodeID node);
         
         void updateDropoutDecision(FieldID field, const DropoutDecision& decision);
     };
     ```

5. **Implement HintsPresenter**
   - Extract hint logic from dialogs
   - Provide CRUD for all hint types

6. **Define presenter interfaces (abstract if needed)**
   - Create `IProjectPresenter`, `IRenderPresenter` etc. if testability requires
   - Allow mock implementations for testing

**Deliverables:**
- ✅ Presenter module compiles independently
- ✅ All business logic extracted from GUI
- ✅ Presenters tested in isolation
- ✅ Documentation for each presenter

---

### Phase 3: GUI Migration (7 days)

**Goal:** Convert all GUI files to use only public API and presenters

**Migration Order (by dependency):**

1. **Low-dependency dialogs** (Day 1-2)
   - `logging.h/cpp` - Already done ✅
   - `aboutdialog.h/cpp`
   - `settingsdialog.h/cpp`
   
2. **Analysis dialogs** (Day 3)
   - `snranalysisdialog.h/cpp`
   - `dropoutanalysisdialog.h/cpp`
   - `burstanalysisdialog.h/cpp`
   - `qualitymetricsdialog.h/cpp`
   - Replace core includes with `AnalysisPresenter`

3. **Hint dialog (metadata hints)** (Day 4)
    - `hintsdialog.h/cpp`
    - Replace core includes with `HintsPresenter` (parity, phase, active-line, dropout/video parameter hints only)

4. **VBI observation dialog** (Day 4-5)
    - `vbidialog.h/cpp`
    - Keep VBI observer/decoder data path (not part of hints); refactor to use presenter/public API access to VBI observations from render/analysis pipeline, no direct core includes

5. **Preview and inspection** (Day 5)
   - `previewdialog.h/cpp`
   - `inspection_dialog.h/cpp`
   - `ntscobserverdialog.h/cpp`
   - Replace core includes with `RenderPresenter`

6. **Dropout editor** (Day 6)
   - `dropout_editor_dialog.h/cpp`
   - Use `AnalysisPresenter` and `RenderPresenter`

7. **Graph components** (Day 7)
   - `orcgraphmodel.h/cpp`
   - `orcgraphicsscene.h/cpp`
   - `orcgraphview.h/cpp`
   - `node_type_helper.h/cpp`
   - Use `ProjectPresenter`

8. **Main window and coordinator** (Day 8)
   - `render_coordinator.h/cpp`
   - `mainwindow.h/cpp`
   - Use all presenters
   - Remove all business logic

**Per-File Migration Process:**

For each file:
1. **Audit**: List all core header includes
2. **Identify**: Which presenter provides needed functionality
3. **Refactor**: Replace direct core calls with presenter calls
4. **Remove**: Delete all `#include "../core/*"` lines
5. **Add**: Include only `<orc_api.h>` and presenter headers
6. **Test**: Verify functionality unchanged
7. **Commit**: Small, atomic commit per file

**Example Migration:**

```cpp
// BEFORE: orc/gui/snranalysisdialog.h
#include "../core/analysis/snr_analysis_types.h"  // ❌ Direct core include
#include "../core/include/node_id.h"              // ❌ Direct core include

class SNRAnalysisDialog {
    void loadData(const orc::SNRAnalysisResult& data);  // ❌ Core type
};

// AFTER: orc/gui/snranalysisdialog.h  
#include <orc_api.h>                              // ✅ Public API
#include "presenters/analysis_presenter.h"         // ✅ Presenter

class SNRAnalysisDialog {
    void loadData(const SNRAnalysisData& data);   // ✅ Public type
private:
    std::unique_ptr<AnalysisPresenter> presenter_;
};
```

**Deliverables:**
- ✅ All GUI files migrated (0 core includes)
- ✅ GUI compiles with only public includes
- ✅ All dialogs functional
- ✅ No business logic in GUI

---

### Phase 4: CLI Migration (2 days)

**Goal:** Ensure CLI uses only public API

**Tasks:**

1. **Audit `orc/cli/main.cpp`**
   - Identify all core header includes
   - Map to public API equivalents

2. **Refactor command handlers**
   - Use public API for project loading
   - Use public API for rendering
   - Use public API for analysis queries

3. **Remove core dependencies**
   ```cmake
   # orc/cli/CMakeLists.txt
   target_link_libraries(orc-cli PRIVATE
       orc-public      # ✅ Only public API
       # NOT orc-core  # ❌ No direct core access
   )
   
   target_include_directories(orc-cli PRIVATE
       ${CMAKE_SOURCE_DIR}/orc/public  # ✅ Only public headers
   )
   ```

4. **Test CLI functionality**
   - Verify all commands work
   - Check help output
   - Validate error handling

5. **Unified Logging Options**
    - Mirror GUI logging in CLI using public logging API
    - Add `--log-level <level>` and `--log-file <path>` options
    - Initialize core logging via `orc/public/orc_logging.h`
    - Use a single shared log file for both CLI and core
    - Example:
      ```bash
      orc-cli --log-level debug --log-file /tmp/orc.log
      ```
    - Help text: `--log-level`: trace|debug|info|warn|error|critical|off; `--log-file`: writes both CLI and core logs

**Deliverables:**
- ✅ CLI compiles with only public API
- ✅ All commands functional
- ✅ No core includes in CLI
 - ✅ CLI `--log-level` and `--log-file` control unified logging

---

### Phase 5: Core Isolation (3 days)

**Goal:** Ensure core is completely private and self-contained

**Tasks:**

1. **Mark core headers as private**
   ```cmake
   # orc/core/CMakeLists.txt
   add_library(orc-core STATIC
       # ... sources
   )
   
   # PRIVATE include directories - not propagated
   target_include_directories(orc-core PRIVATE
       ${CMAKE_CURRENT_SOURCE_DIR}/include
       ${CMAKE_CURRENT_SOURCE_DIR}/stages
       ${CMAKE_CURRENT_SOURCE_DIR}/analysis
       ${CMAKE_CURRENT_SOURCE_DIR}/hints
       ${CMAKE_CURRENT_SOURCE_DIR}/util
   )
   
   # Only common types are public interface
   target_link_libraries(orc-core PUBLIC orc-common)
   ```

2. **Remove core from public API build interface**
   ```cmake
   # orc/public/CMakeLists.txt
   target_link_libraries(orc-public PRIVATE orc-core)  # PRIVATE not PUBLIC
   ```

3. **Verify no accidental core exposure**
   ```bash
   # Check no core headers in public install
   find build/include -name "*" -type f | grep -i core
   # Should return nothing
   ```

4. **Create core internal documentation**
   - Document core module structure
   - Explain what should/shouldn't be in core
   - Provide examples of proper core usage

**Deliverables:**
- ✅ Core headers not in build interface
- ✅ Only common types visible externally
- ✅ Core documentation complete

---

## Build System Enforcement

### CMake Configuration

**1. Strict Include Directories**

```cmake
# orc/gui/CMakeLists.txt - ENFORCE SEPARATION
target_include_directories(orc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/public         # ✅ ONLY public API
    ${CMAKE_CURRENT_SOURCE_DIR}            # ✅ Own headers
    # ${CMAKE_SOURCE_DIR}/orc/core         # ❌ REMOVED - no core access
)

target_link_libraries(orc-gui PRIVATE
    orc-presenters                          # ✅ Through presenters
    orc-public                             # ✅ Public API
    # NOT orc-core                         # ❌ No direct link
    Qt6::Widgets
    Qt6::Gui
)
```

**2. Interface Libraries for Clean Dependencies**

```cmake
# orc/common/CMakeLists.txt
add_library(orc-common INTERFACE)
target_include_directories(orc-common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/orc/common>
)

# orc/public/CMakeLists.txt  
add_library(orc-public STATIC impl/public_api_impl.cpp)
target_include_directories(orc-public PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/orc/public>
)
target_include_directories(orc-public PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/core/include    # Only impl sees core
)
target_link_libraries(orc-public
    PUBLIC orc-common                       # Public interface
    PRIVATE orc-core                        # Private implementation
)
```

**3. Build-Time Validation Target**

```cmake
# Add to top-level CMakeLists.txt
add_custom_target(validate-mvp
    COMMAND ${CMAKE_SOURCE_DIR}/orc/public/validate_mvp.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Validating MVP architecture compliance"
)

# Run automatically before tests
add_dependencies(test validate-mvp)
```

**4. Install Rules Enforce Separation**

```cmake
# Only install public headers
install(DIRECTORY orc/public/
    DESTINATION include/orc
    FILES_MATCHING PATTERN "*.h"
    PATTERN "impl" EXCLUDE          # Don't install implementation details
)

install(DIRECTORY orc/common/include/
    DESTINATION include/orc/common
    FILES_MATCHING PATTERN "*.h"
)

# Core headers never installed
# (no install rules for orc/core/include)
```

---

## Compiler Enforcement

### 1. Include Guards and Namespace Protection

**Mark internal headers clearly:**

```cpp
// orc/core/include/internal_stage.h
#ifndef ORC_CORE_INTERNAL_STAGE_H_
#define ORC_CORE_INTERNAL_STAGE_H_

#ifdef ORC_PUBLIC_API_BUILD
#error "Core internal headers cannot be used in public API builds"
#endif

namespace orc::internal {  // Use ::internal namespace
    // ... implementation
}

#endif
```

**Define build flag in core only:**

```cmake
# orc/core/CMakeLists.txt
target_compile_definitions(orc-core PRIVATE ORC_CORE_BUILD)

# orc/public/CMakeLists.txt
target_compile_definitions(orc-public PRIVATE ORC_PUBLIC_API_BUILD)
```

### 2. Symbol Visibility Control

**Hide core symbols:**

```cmake
# orc/core/CMakeLists.txt
set_target_properties(orc-core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

# Explicitly export only public API
target_compile_definitions(orc-public PUBLIC
    ORC_API_EXPORT  # For dllexport/visibility attributes
)
```

**Use visibility macros:**

```cpp
// orc/common/include/orc_export.h
#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #ifdef ORC_API_EXPORT
        #define ORC_API __declspec(dllexport)
    #else
        #define ORC_API __declspec(dllimport)
    #endif
#else
    #define ORC_API __attribute__((visibility("default")))
#endif

// orc/public/orc_api.h
#include <orc_export.h>

extern "C" {
    ORC_API OrcProjectHandle orc_project_create(const char* path);
    ORC_API void orc_project_destroy(OrcProjectHandle handle);
}
```

### 3. Compiler Warnings as Errors

```cmake
# Enforce strict compilation
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(orc-gui PRIVATE
        -Wall
        -Wextra
        -Werror                      # Warnings are errors
        -Wno-unused-parameter        # Allow in GUI for Qt signals
    )
endif()

if(MSVC)
    target_compile_options(orc-gui PRIVATE
        /W4                          # Warning level 4
        /WX                          # Warnings as errors
    )
endif()
```

### 4. Include-What-You-Use Integration

```cmake
# Optional: IWYU for strict include checking
find_program(IWYU_PATH NAMES include-what-you-use iwyu)
if(IWYU_PATH)
    set_target_properties(orc-gui PROPERTIES
        CXX_INCLUDE_WHAT_YOU_USE "${IWYU_PATH};-Xiwyu;--no_fwd_decls"
    )
endif()
```

### 5. Clang-Tidy Enforcement

```cmake
# .clang-tidy file in project root
find_program(CLANG_TIDY_PATH NAMES clang-tidy)
if(CLANG_TIDY_PATH)
    set(CLANG_TIDY_COMMAND
        "${CLANG_TIDY_PATH}"
        "-checks=-*,misc-include-cleaner"
        "-warnings-as-errors=*"
    )
    set_target_properties(orc-gui PROPERTIES
        CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}"
    )
endif()
```

---

## Migration Strategy

### Incremental Migration Approach

**Principle:** Maintain working build at all times

**Strategy:**

1. **Dual-Mode Operation (Temporary)**
   ```cmake
   # orc/gui/CMakeLists.txt - TEMPORARY during migration
   option(ORC_STRICT_MVP "Enforce strict MVP architecture" OFF)
   
   if(ORC_STRICT_MVP)
       # New way: only public API
       target_include_directories(orc-gui PRIVATE
           ${CMAKE_SOURCE_DIR}/orc/public
       )
   else()
       # Old way: direct core access (for unmigrated files)
       target_include_directories(orc-gui PRIVATE
           ${CMAKE_SOURCE_DIR}/orc/core/include
           ${CMAKE_SOURCE_DIR}/orc/public
       )
   endif()
   ```

2. **File-by-File Migration Tracking**
   
   Create `orc/gui/MIGRATION_STATUS.md`:
   ```markdown
   # GUI Migration Status
   
   ## Completed (3/45)
   - [x] logging.h/cpp
   - [x] guiproject.h/cpp
   - [x] aboutdialog.h/cpp
   
   ## In Progress (1/45)
   - [ ] snranalysisdialog.h/cpp (blocked: needs AnalysisPresenter)
   
   ## Blocked (5/45)
   - [ ] mainwindow.h/cpp (needs: ProjectPresenter, RenderPresenter)
   - [ ] render_coordinator.h/cpp (needs: RenderPresenter)
   ...
   
   ## Not Started (36/45)
   ...
   ```

3. **Feature Flags for Risky Changes**
   ```cpp
   // Use preprocessor to maintain both paths temporarily
   #ifdef ORC_USE_NEW_RENDER_API
       auto image = renderPresenter_->renderPreview(nodeId, fieldId);
   #else
       auto image = oldRenderCoordinator_->renderPreview(nodeId, fieldId);
   #endif
   ```

4. **Parallel Implementation**
   - Keep old code working
   - Implement new presenter-based code alongside
   - Switch over when validated
   - Remove old code in cleanup commit

### Testing Strategy During Migration

1. **Regression Test Suite**
   - Capture current behavior as baseline
   - Run after each file migration
   - Ensure no behavioral changes

2. **A/B Testing**
   - Run old and new code paths in parallel
   - Compare outputs
   - Flag discrepancies

3. **Staged Rollout**
   - Enable new code for one dialog at a time
   - Get user feedback
   - Fix issues before proceeding

---

## Validation & Testing

### Automated Validation Script

**Enhanced `orc/public/validate_mvp.sh`:**

```bash
#!/bin/bash
# validate_mvp.sh - Comprehensive MVP architecture validation

set -e

echo "=== MVP Architecture Validation ==="

# 1. Check GUI doesn't include core headers
echo "Checking GUI layer..."
GUI_VIOLATIONS=$(grep -r "#include.*\.\./core" orc/gui/ || true)
if [ -n "$GUI_VIOLATIONS" ]; then
    echo "❌ FAIL: GUI includes core headers:"
    echo "$GUI_VIOLATIONS"
    exit 1
fi
echo "✅ GUI layer clean"

# 2. Check CLI doesn't include core headers
echo "Checking CLI layer..."
CLI_VIOLATIONS=$(grep -r "#include.*\.\./core" orc/cli/ || true)
if [ -n "$CLI_VIOLATIONS" ]; then
    echo "❌ FAIL: CLI includes core headers:"
    echo "$CLI_VIOLATIONS"
    exit 1
fi
echo "✅ CLI layer clean"

# 3. Check public API doesn't expose core types
echo "Checking public API..."
PUBLIC_LEAKS=$(grep -r "namespace orc" orc/public/*.h | grep -v "orc::public_api" || true)
if [ -n "$PUBLIC_LEAKS" ]; then
    echo "❌ FAIL: Public API exposes core namespaces:"
    echo "$PUBLIC_LEAKS"
    exit 1
fi
echo "✅ Public API isolated"

# 4. Check presenters don't expose core to GUI
echo "Checking presenter layer..."
PRESENTER_LEAKS=$(grep -r "#include.*core/" orc/presenters/include/ || true)
if [ -n "$PRESENTER_LEAKS" ]; then
    echo "❌ FAIL: Presenter headers include core:"
    echo "$PRESENTER_LEAKS"
    exit 1
fi
echo "✅ Presenter layer clean"

# 5. Verify build system configuration
echo "Checking CMake configuration..."
if grep -q "orc/core/include" orc/gui/CMakeLists.txt; then
    echo "❌ FAIL: GUI CMakeLists still includes core directories"
    exit 1
fi
echo "✅ Build system enforces separation"

# 6. Check for business logic in GUI
echo "Checking for business logic patterns in GUI..."
LOGIC_PATTERNS="if.*VideoFormat|switch.*NodeType|for.*stages"
LOGIC_VIOLATIONS=$(grep -rE "$LOGIC_PATTERNS" orc/gui/*.cpp | grep -v "presenter->" || true)
if [ -n "$LOGIC_VIOLATIONS" ]; then
    echo "⚠️  WARNING: Possible business logic in GUI:"
    echo "$LOGIC_VIOLATIONS"
    # Warning only, not fatal
fi

echo ""
echo "=== ✅ All MVP validation checks passed ==="
```

### Unit Tests for Each Layer

**Test structure:**

```
orc/
├── common/tests/
│   └── test_field_id.cpp
├── core/tests/
│   ├── test_stages.cpp
│   └── test_analysis.cpp
├── public/tests/
│   ├── test_public_api.cpp
│   └── test_handles.cpp
├── presenters/tests/
│   ├── test_project_presenter.cpp
│   └── test_render_presenter.cpp
└── gui/tests/
    ├── test_main_window.cpp     # No core logic to test
    └── test_dialogs.cpp
```

**Example presenter test:**

```cpp
// orc/presenters/tests/test_project_presenter.cpp
#include <gtest/gtest.h>
#include "presenters/project_presenter.h"

TEST(ProjectPresenter, CreateQuickProjectNTSC) {
    auto handle = ProjectPresenter::createQuickProject(
        VideoFormat::NTSC,
        {"input1.tbc", "input2.tbc"}
    );
    
    ASSERT_NE(handle, nullptr);
    
    auto stages = ProjectPresenter::getAvailableStages(
        handle,
        VideoFormat::NTSC
    );
    
    // Verify NTSC-specific stages present
    EXPECT_TRUE(hasStage(stages, "ntsc-comb-decode"));
    EXPECT_FALSE(hasStage(stages, "pal-transform-2d"));
    
    orc_project_destroy(handle);
}
```

### Integration Tests

```cpp
// orc/tests/integration/test_end_to_end.cpp
TEST(Integration, GUIToCore) {
    // Verify full stack works without direct core access
    
    // 1. GUI creates project via presenter
    ProjectPresenter presenter;
    auto project = presenter.createQuickProject(/*...*/);
    
    // 2. GUI requests render via presenter
    RenderPresenter renderer(project);
    auto preview = renderer.renderPreview(nodeId, fieldId);
    
    // 3. Verify image produced
    EXPECT_TRUE(preview.isValid());
    EXPECT_GT(preview.width(), 0);
    
    // 4. No core headers were included in this test
    // (enforced by test CMakeLists.txt)
}
```

---

## Risk Mitigation

### Identified Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Type conflicts during migration | High | High | Phase 1 resolves before GUI migration |
| Breaking existing functionality | Medium | Critical | Comprehensive regression tests |
| Performance degradation | Low | Medium | Profile before/after; optimize presenters |
| Team resistance to architecture | Medium | Medium | Clear documentation; pair programming |
| Migration takes longer than planned | Medium | Low | Incremental approach; can pause anytime |
| Incomplete presenter API | High | High | Design presenters with GUI team input |

### Mitigation Strategies

1. **Type Conflict Resolution**
   - Complete Phase 1 entirely before starting GUI migration
   - Validate no conflicts with test compilation
   - Document type conversion patterns

2. **Functionality Preservation**
   - Create comprehensive test suite before migration
   - Use A/B testing during migration
   - Keep old code until new code validated

3. **Performance Monitoring**
   - Benchmark critical paths before migration
   - Profile after each major change
   - Optimize presenter layer if needed (inline small functions)

4. **Team Buy-In**
   - Present plan to team for review
   - Pair program on first few migrations
   - Document patterns and examples clearly

5. **Timeline Flexibility**
   - Use dual-mode build system
   - Can ship with partial migration
   - Finish remaining files in future sprints

---

## Success Criteria

### Quantitative Metrics

- [ ] **Zero** core header includes in GUI files
- [ ] **Zero** core header includes in CLI files
- [ ] **Zero** business logic in GUI (measured by complexity analysis)
- [ ] **100%** of tests passing
- [ ] **< 5%** performance regression
- [ ] **Zero** type definition conflicts

### Qualitative Goals

- [ ] GUI developers can work without understanding core internals
- [ ] Public API is documented and understandable
- [ ] Presenters are unit-testable without Qt
- [ ] Core can be refactored without touching GUI
- [ ] New analysis types can be added without GUI changes
- [ ] CLI and GUI share all business logic (no duplication)

### Build System Verification

```bash
# These must all pass:
./orc/public/validate_mvp.sh
cmake -DORC_STRICT_MVP=ON ..
make -j$(nproc)
make test
```

### Code Review Checklist

Before marking migration complete, verify:

- [ ] No `#include "../core/*"` in gui/ or cli/
- [ ] All business logic in core/ or presenters/
- [ ] Public API has no core namespace leaks
- [ ] CMakeLists.txt enforces separation
- [ ] Documentation updated
- [ ] All tests passing
- [ ] Performance benchmarks acceptable
- [ ] validate_mvp.sh passes
- [ ] Can build with `-DORC_STRICT_MVP=ON`

---

## Timeline

### Gantt Chart (4-week plan)

```
Week 1:
  Phase 0: Preparation              ████░░░░░░░░░░░░░░░░
  Phase 1: Type Unification         ░░░░██████░░░░░░░░░░

Week 2:
  Phase 2: Presenter Layer          ░░░░░░░░░████████████
  
Week 3:
  Phase 3: GUI Migration (Part 1)   ░░░░░░░░░░░░░░██████
  
Week 4:
  Phase 3: GUI Migration (Part 2)   ████████░░░░░░░░░░░░
  Phase 4: CLI Migration            ░░░░░░░░████░░░░░░░░
  Phase 5: Core Isolation           ░░░░░░░░░░░░████░░░░
  Final validation & cleanup        ░░░░░░░░░░░░░░░░████
```

### Critical Path

1. Phase 1 (Type Unification) - **MUST** complete before Phase 3
2. Phase 2 (Presenters) - **MUST** complete before Phase 3
3. Phases 3-5 can overlap partially

### Milestones

- **Day 5**: Types unified, no conflicts ✅
- **Day 10**: All presenters implemented ✅
- **Day 15**: 50% of GUI migrated ✅
- **Day 20**: All GUI migrated ✅
- **Day 22**: CLI migrated ✅
- **Day 25**: All tests passing ✅
- **Day 28**: Documentation complete, ready to merge ✅

---

## Conclusion

This implementation plan provides a clear, systematic approach to establishing proper MVP architecture in the decode-orc codebase. By following the phases sequentially and validating at each step, we can eliminate the 64+ architecture violations while maintaining a working build throughout the migration.

The key to success is:
1. **Resolve type conflicts first** (Phase 1)
2. **Create presenters before migrating GUI** (Phase 2)
3. **Migrate incrementally** with validation (Phase 3-5)
4. **Enforce at build and compile time** (ongoing)

Once complete, the architecture will be:
- ✅ Properly separated into layers
- ✅ Enforced by build system
- ✅ Checked by compiler
- ✅ Validated by automated scripts
- ✅ Maintainable and testable
- ✅ Ready for future evolution

**Next Step:** Review this plan with the team, then begin Phase 0.

---

**Document Status:** Draft  
**Next Review:** 2026-01-23  
**Owner:** Development Team  
**Related Docs:** [mvp-architecture-violations.md](mvp-architecture-violations.md)
