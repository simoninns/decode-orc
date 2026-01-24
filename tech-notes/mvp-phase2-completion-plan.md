# MVP Architecture Phase 2 Completion Plan

**Date:** 2026-01-23  
**Status:** Active Implementation Plan  
**Phase:** Completing MVP Architecture Migration

## Executive Summary

This plan completes the MVP architecture migration from the current partially-migrated state to a fully-enforced, compiler-validated architecture with zero core dependencies in GUI/CLI layers.

**Current State:**
- ✅ No `#include "../core/*"` relative paths in GUI/CLI
- ✅ Presenter layer exists with 13 files
- ✅ Public API exists
- ✅ Common types module exists
- ✅ Analysis bridge library isolates 11 analysis framework files
- ❌ GUI still includes core headers directly (project.h, preview_renderer.h, stage_parameter.h, project_to_dag.h)
- ❌ CMakeLists.txt exposes all core directories to GUI
- ❌ No compiler enforcement - only script validation
- ❌ render_coordinator.cpp acts as implementation bridge (acknowledged with TODO)
- ❌ 11 files in orc/gui have direct core header dependencies

**Goal State:**
- Zero core header includes in GUI/CLI (enforced by compiler)
- All GUI/CLI code uses only public API and presenters
- Build system prevents core access (compile-time enforcement)
- All bridge code eliminated or properly isolated
- Clean architectural boundaries

**Estimated Effort:** 2-3 weeks  
**Risk:** Medium - requires coordination but clear path

---

## Table of Contents

1. [Current Architecture Analysis](#current-architecture-analysis)
2. [Remaining Violations](#remaining-violations)
3. [Implementation Phases](#implementation-phases)
4. [Compiler Enforcement Strategy](#compiler-enforcement-strategy)
5. [Migration Patterns](#migration-patterns)
6. [Validation & Testing](#validation--testing)
7. [Success Criteria](#success-criteria)

---

## Current Architecture Analysis

### Files with Direct Core Dependencies

**render_coordinator.{h,cpp}** (2 files)
- Includes: `project_to_dag.h`, `preview_renderer.h`, `dag_executor.h`, `observation_context.h`, `vbi_decoder.h`, `dropout_analysis_sink_stage.h`, `snr_analysis_sink_stage.h`, `burst_level_analysis_sink_stage.h`, `ld_sink_stage.h`
- Role: Implementation bridge between GUI and core rendering/analysis
- Status: Acknowledged bridge with TODO(MVP) comment

**guiproject.{h,cpp}** (2 files)
- Includes: `project.h`, `project_to_dag.h`
- Role: Wraps core Project for GUI use
- Status: Should use ProjectPresenter instead

**stageparameterdialog.h** (1 file)
- Includes: `stage_parameter.h`
- Role: Dialog for editing stage parameters
- Status: Should use presenter or public API types

**configdialogbase.h** (1 file)
- Includes: `stage_parameter.h`
- Role: Base class for config dialogs
- Status: Should use presenter or public API types

**fieldpreviewwidget.h** (1 file)
- Includes: `preview_renderer.h`
- Role: Widget for displaying field previews
- Status: Should use presenter types

**mainwindow.h** (1 file)
- Includes: `preview_renderer.h` (via guiproject.h)
- Role: Main application window
- Status: Should use presenter types

**orcgraphicsscene.cpp** (1 file)
- Includes: `analysis_registry.h`
- Role: Graph scene with analysis menu
- Status: Should use AnalysisPresenter

**Total:** 11 files with 8+ unique core header dependencies

### Bridge Components

**orc-gui-analysis-bridge library** (11 files)
- Purpose: Isolates generic analysis framework that uses core types directly
- Files: `analysis_dialog.{h,cpp}`, `analysis_runner.{h,cpp}`, `analysis_progress_impl.{h,cpp}`, `vectorscope_dialog.{h,cpp}`
- Status: Properly isolated but should be migrated or documented as permanent bridge

---

## Remaining Violations

### Category 1: Type Exposure (High Priority)

These core types are used directly in GUI code:

1. **`orc::Project`** - Core project type
   - Used in: guiproject.h, mainwindow (indirectly)
   - Solution: ProjectPresenter should expose only needed operations, never the core Project

2. **`ParameterValue`, `ParameterType`, `ParameterDescriptor`** - From stage_parameter.h
   - Used in: stageparameterdialog.h, configdialogbase.h
   - Solution: Move to common/ or re-export via public API

3. **`PreviewRenderResult`, `PreviewOutputType`** - From preview_renderer.h
   - Used in: fieldpreviewwidget.h, mainwindow.h, render_coordinator.h
   - Solution: Create presenter wrapper types or move to public API

4. **`orc::AnalysisTool`, `AnalysisContext`, `AnalysisResult`** - From core analysis
   - Used in: analysis bridge files, orcgraphicsscene.cpp
   - Solution: Move to public API or wrap in AnalysisPresenter

### Category 2: Function Calls (High Priority)

These core functions are called directly from GUI:

1. **`orc::project_to_dag()`**
   - Used in: guiproject.cpp, render_coordinator.cpp
   - Solution: Add `ProjectPresenter::buildDAG()` method

2. **`orc::validate_source_nodes()`**
   - Used in: guiproject.cpp (currently commented out)
   - Solution: Handle in ProjectPresenter or remove if not needed

3. **`orc::AnalysisRegistry::instance()`**
   - Used in: orcgraphicsscene.cpp
   - Solution: Add `AnalysisPresenter::getAvailableAnalysisTools(stage_name)` method

### Category 3: Build System (Medium Priority)

CMakeLists.txt exposes core directories:
- `orc/core/include`
- `orc/core/analysis`
- `orc/core/stages/ld_sink`
- `orc/core/stages/dropout_analysis_sink`
- `orc/core/stages/snr_analysis_sink`
- `orc/core/stages/burst_level_analysis_sink`

---

## Implementation Phases

### Phase 2.1: Type Migration to Common/Public (3 days)

**Goal:** Move shared types out of core internal headers

**Tasks:**

1. **Move parameter types to common module**
   ```bash
   # Move these to orc/common/include/
   - stage_parameter.h → parameter_types.h
   ```
   
   Create `orc/common/include/parameter_types.h`:
   ```cpp
   #pragma once
   #include <string>
   #include <variant>
   #include <vector>
   
   namespace orc {
   
   using ParameterValue = std::variant<
       int32_t, uint32_t, double, bool, std::string
   >;
   
   enum class ParameterType {
       INT32, UINT32, DOUBLE, BOOL, STRING, FILE_PATH
   };
   
   struct ParameterDependency {
       std::string parameter_name;
       std::vector<std::string> required_values;
   };
   
   struct ParameterConstraints {
       std::optional<ParameterValue> min_value;
       std::optional<ParameterValue> max_value;
       std::optional<ParameterValue> default_value;
   };
   
   struct ParameterDescriptor {
       std::string name;
       std::string display_name;
       std::string description;
       ParameterType type;
       ParameterConstraints constraints;
       std::vector<ParameterDependency> dependencies;
       bool is_optional;
   };
   
   } // namespace orc
   ```

2. **Create public API rendering types**
   
   Create `orc/public/orc_rendering.h`:
   ```cpp
   #pragma once
   #include <cstdint>
   #include <vector>
   #include <string>
   
   namespace orc::public_api {
   
   enum class PreviewOutputType {
       SOURCE,
       LUMA,
       CHROMA,
       COMPOSITE,
       RGB
   };
   
   struct PreviewRenderResult {
       std::vector<uint8_t> rgb_data;
       int width;
       int height;
       int stride;
       bool is_valid;
       std::string error_message;
   };
   
   struct RenderProgress {
       int current_field;
       int total_fields;
       std::string status_message;
       bool is_complete;
       bool has_error;
   };
   
   } // namespace orc::public_api
   ```

3. **Update core to use common types**
   - Update `orc/core/include/stage_parameter.h` to include from common
   - Verify core builds

**Deliverables:**
- ✅ parameter_types.h in common module
- ✅ orc_rendering.h in public API
- ✅ Core builds successfully
- ✅ No duplicate type definitions

---

### Phase 2.2: Render Presenter Enhancement (4 days)

**Goal:** Create complete rendering interface in presenter layer

**Tasks:**

1. **Enhance RenderPresenter with all needed operations**
   
   Update `orc/presenters/include/render_presenter.h`:
   ```cpp
   class RenderPresenter {
   public:
       // Constructor takes Project reference (not pointer)
       explicit RenderPresenter(orc::Project& project);
       
       // Preview rendering
       PreviewRenderResult renderPreview(
           NodeID node,
           FieldID field,
           PreviewOutputType output_type
       );
       
       // Batch rendering
       uint64_t startBatchRender(
           NodeID node,
           const std::vector<FieldID>& fields,
           ProgressCallback callback
       );
       
       bool cancelBatchRender(uint64_t request_id);
       
       RenderProgress getBatchProgress(uint64_t request_id) const;
       
       // Analysis data requests
       uint64_t requestDropoutData(NodeID node, ProgressCallback callback);
       uint64_t requestSNRData(NodeID node, ProgressCallback callback);
       uint64_t requestBurstData(NodeID node, ProgressCallback callback);
       
       // VBI decoding
       VBIData getVBIData(NodeID node, FieldID field);
       
       // Observations
       ObservationData getObservations(NodeID node, FieldID field);
   };
   ```

2. **Implement rendering delegation**
   
   In `orc/presenters/src/render_presenter.cpp`:
   - Create internal DAG from project
   - Delegate to core rendering engine
   - Convert core types to presenter types
   - Handle all threading/progress internally

3. **Remove render_coordinator.cpp dependency on core rendering**
   
   Refactor `orc/gui/render_coordinator.cpp`:
   - Use RenderPresenter instead of direct core calls
   - Remove includes: `preview_renderer.h`, `dag_executor.h`, `project_to_dag.h`
   - Keep only: presenter includes, public API types
   - **Note:** Analysis sink stage headers remain temporarily (removed in Phase 2.4)

**Deliverables:**
- ✅ RenderPresenter fully implemented
- ✅ render_coordinator uses only presenter for rendering operations
- ✅ Removed preview_renderer.h, dag_executor.h, project_to_dag.h from render_coordinator
- ✅ All rendering features working
- ⏸️ Analysis sink stage headers remain (dropout_analysis_sink_stage.h, snr_analysis_sink_stage.h, burst_level_analysis_sink_stage.h, ld_sink_stage.h) - to be removed in Phase 2.4

---

### Phase 2.3: Project Presenter Enhancement (3 days)

**Goal:** Complete project management interface

**Tasks:**

1. **Add DAG building to ProjectPresenter**
   
   ```cpp
   // In orc/presenters/include/project_presenter.h
   class ProjectPresenter {
   public:
       // DAG operations
       std::shared_ptr<void> buildDAG();  // Returns opaque DAG handle
       bool validateDAG();
       
       // Project operations that need DAG
       bool triggerNode(NodeID node, ProgressCallback callback);
       
       // Parameter access
       std::vector<ParameterDescriptor> getStageParameters(
           const std::string& stage_name
       );
       
       std::map<std::string, ParameterValue> getNodeParameters(NodeID node);
       
       bool setNodeParameters(
           NodeID node,
           const std::map<std::string, ParameterValue>& params
       );
   };
   ```

2. **Refactor GUIProject to use only ProjectPresenter**
   
   In `orc/gui/guiproject.{h,cpp}`:
   - Remove `#include "project.h"`
   - Remove `#include "project_to_dag.h"`
   - Remove `orc::Project core_project_` member
   - Add `std::unique_ptr<ProjectPresenter> presenter_` member
   - Delegate all operations to presenter

3. **Update all GUIProject usage**
   - mainwindow.cpp: Use presenter methods
   - No direct access to core_project_

**Deliverables:**
- ✅ ProjectPresenter has all needed methods
- ✅ GUIProject has zero core includes
- ✅ All project operations working

---

### Phase 2.4: Analysis Presenter Enhancement (3 days)

**Goal:** Complete analysis interface and eliminate direct DAG access from render_coordinator

**Tasks:**

1. **Move analysis types to public API**
   
   Create `orc/public/orc_analysis.h`:
   ```cpp
   namespace orc::public_api {
   
   struct AnalysisToolInfo {
       std::string name;
       std::string description;
       int priority;
       std::vector<std::string> applicable_stages;
   };
   
   enum class AnalysisStatus {
       NotStarted,
       Running,
       Complete,
       Failed,
       Cancelled
   };
   
   struct AnalysisProgress {
       int current;
       int total;
       std::string status_message;
       std::string sub_status;
       AnalysisStatus status;
   };
   
   } // namespace orc::public_api
   ```

2. **Add tool registry to AnalysisPresenter**
   
   ```cpp
   class AnalysisPresenter {
   public:
       // Tool discovery
       std::vector<AnalysisToolInfo> getAvailableTools(
           const std::string& stage_name
       );
       
       AnalysisToolInfo getToolInfo(const std::string& tool_name);
       
       // Run analysis
       uint64_t runAnalysis(
           const std::string& tool_name,
           NodeID node,
           const std::map<std::string, ParameterValue>& params,
           ProgressCallback callback
       );
       
       bool cancelAnalysis(uint64_t analysis_id);
       
       AnalysisProgress getAnalysisProgress(uint64_t analysis_id);
   };
   ```

3. **Refactor orcgraphicsscene.cpp**
   - Remove `#include "analysis_registry.h"`
   - Use `AnalysisPresenter::getAvailableTools()` instead

4. **Remove direct DAG access from render_coordinator.cpp**
   - Implement `RenderPresenter::getAnalysisData()` methods to abstract sink stage access
   - Remove includes: `dropout_analysis_sink_stage.h`, `snr_analysis_sink_stage.h`, `burst_level_analysis_sink_stage.h`, `ld_sink_stage.h`
   - Replace direct DAG traversal with presenter method calls
   - Move sink stage access logic into RenderPresenter implementation

**Deliverables:**
- ✅ AnalysisPresenter complete with tool registry methods
- ✅ orcgraphicsscene uses only presenter (analysis_registry.h removed)
- ✅ No analysis_registry access from GUI
- ✅ Analysis sink stage headers removed from render_coordinator.cpp (dropout, SNR, burst)
- ✅ Analysis data retrieval abstracted through RenderPresenter
- ✅ Analysis result types use common_types.h instead of separate core headers
- ⏸️ ld_sink_stage.h remains for TriggerableStage interface (trigger migration is future work)
- ⏸️ handleTriggerStage() still uses direct DAG access (will be migrated in future phase)

**Remaining Core Dependencies in render_coordinator.cpp:**
- `ld_sink_stage.h` - for TriggerableStage interface (used in handleTriggerStage)
- `preview_renderer.h` - for PreviewOutputInfo, FrameLineNavigationResult types in signals

**Note:** These remaining dependencies are for functionality not yet migrated to presenters:
- Trigger operations (handleTriggerStage) - requires RenderPresenter enhancement or new TriggerPresenter
- Signal types - requires migrating these types to public API (covered in Phase 2.6)

---

### Phase 2.5: Parameter Dialog Migration (2 days)

**Goal:** Eliminate stage_parameter.h dependencies

**Tasks:**

1. **Update stageparameterdialog.h**
   - Change `#include "stage_parameter.h"` to `#include <parameter_types.h>`
   - Verify uses only common types

2. **Update configdialogbase.h**
   - Change `#include "stage_parameter.h"` to `#include <parameter_types.h>`
   - Verify uses only common types

3. **Test all parameter dialogs**
   - Stage parameter dialog
   - Mask line config dialog
   - FFmpeg preset dialog

**Deliverables:**
- ✅ All dialogs use common types
- ✅ No stage_parameter.h includes
- ✅ Phase 2.5 COMPLETE (2026-01-24)

---

### Phase 2.6: Preview Widget Migration (2 days)

**Goal:** Eliminate preview_renderer.h dependencies

**Tasks:**

1. **Move preview types to public API**
   
   Update `orc/public/orc_rendering.h` to include signal types:
   ```cpp
   namespace orc::public_api {
   
   struct PreviewOutputInfo {
       orc::PreviewOutputType type;
       std::string display_name;
       uint64_t count;
       bool is_available;
       double dar_aspect_correction;
       std::string option_id;
       bool dropouts_available;
       bool has_separate_channels;
   };
   
   struct FrameLineNavigationResult {
       bool is_valid;
       uint64_t field_index;
       int line_number;
   };
   
   } // namespace orc::public_api
   ```

2. **Update fieldpreviewwidget.h**
   - Change `#include "preview_renderer.h"` to `#include <orc_rendering.h>`
   - Use `PreviewRenderResult` from public API

3. **Update mainwindow.h**
   - Remove `#include "preview_renderer.h"`
   - Use public API rendering types

4. **Update render_coordinator signals**
   - Change signal types to use `orc::public_api::PreviewOutputInfo`
   - Change signal types to use `orc::public_api::FrameLineNavigationResult`
   - Remove `#include "preview_renderer.h"` from render_coordinator.cpp

5. **Test preview functionality**
   - Field preview widget
   - Preview dialog
   - All rendering modes

**Deliverables:**
- ✅ Preview types moved to public API
- ✅ Preview widgets use public API types
- ✅ render_coordinator.cpp has zero preview_renderer.h includes
- ✅ All rendering features working

---

### Phase 2.7: Trigger Operation Migration (3 days)

**Goal:** Eliminate ld_sink_stage.h dependency and abstract trigger operations

**Tasks:**

1. **Move TriggerableStage interface to common**
   
   Create `orc/common/include/triggerable_stage.h`:
   ```cpp
   #pragma once
   #include <functional>
   #include <string>
   #include <cstddef>
   
   namespace orc {
   
   // Forward declarations to avoid core dependencies
   class ObservationContext;
   class Artifact;
   using ArtifactPtr = std::shared_ptr<Artifact>;
   
   using TriggerProgressCallback = std::function<void(
       size_t current, size_t total, const std::string& message
   )>;
   
   class TriggerableStage {
   public:
       virtual ~TriggerableStage() = default;
       
       virtual bool trigger(
           const std::vector<ArtifactPtr>& inputs,
           const std::map<std::string, ParameterValue>& parameters,
           ObservationContext& observation_context
       ) = 0;
       
       virtual std::string get_trigger_status() const = 0;
       virtual void set_progress_callback(TriggerProgressCallback callback) = 0;
       virtual bool is_trigger_in_progress() const = 0;
       virtual void cancel_trigger() = 0;
   };
   
   } // namespace orc
   ```
   
   **OR** (Recommended):

2. **Add trigger methods to RenderPresenter**
   
   ```cpp
   class RenderPresenter {
   public:
       // Trigger operations
       uint64_t triggerStage(
           NodeID node_id,
           ProgressCallback callback
       );
       
       void cancelTrigger();
       bool isTriggerActive() const;
       std::string getTriggerStatus() const;
   };
   ```

3. **Refactor render_coordinator handleTriggerStage**
   - Use `RenderPresenter::triggerStage()` instead of direct DAG access
   - Remove DAG traversal code
   - Remove TriggerableStage casting
   - Remove `#include "ld_sink_stage.h"`

4. **Update ld_sink_stage.h**
   - Change to include `triggerable_stage.h` from common (if Option 1)
   - Verify core stages still build

**Deliverables:**
- ✅ TriggerableStage interface accessible without core headers
- ✅ render_coordinator uses RenderPresenter for triggering
- ✅ No ld_sink_stage.h include in render_coordinator.cpp
- ✅ All trigger functionality working
- ✅ render_coordinator has ZERO direct DAG access

---

### Phase 2.8: Analysis Bridge Decision (2 days)

**Goal:** Decide fate of orc-gui-analysis-bridge library

**Options:**

**Option A: Migrate to Presenter Pattern**
- Create `GenericAnalysisPresenter` class
- Wrap `AnalysisTool` interface
- Move to presenters module

**Option B: Keep as Documented Bridge**
- Accept that generic analysis framework needs core types
- Properly document as architectural exception
- Move to `orc/gui/internal/` directory
- Create separate CMake library with explicit core access

**Option C: Deprecate Generic Framework**
- Remove generic `AnalysisDialog`
- Keep only specific dialogs (SNR, Dropout, Burst)
- Delete unused analysis runner infrastructure

**Recommendation: Option A** - Migrate to presenter pattern
- Most consistent with MVP architecture
- Provides clean interface for future analysis tools
- Eliminates special-case bridge library

**Implementation if Option A chosen:**

1. Create `orc/presenters/include/generic_analysis_presenter.h`
2. Wrap core analysis types in presenter types
3. Move analysis_dialog, analysis_runner to use presenters
4. Delete orc-gui-analysis-bridge library
5. Move files to regular orc/gui build

**Deliverables:**
- ✅ Decision documented
- ✅ Implementation complete per chosen option
- ✅ All analysis functionality working
- ✅ AnalysisPresenter::getToolById() bridge method removed (if Option A)

---

## Compiler Enforcement Strategy

### Build System Changes

**1. Remove Core Includes from GUI CMakeLists.txt**

```cmake
# orc/gui/CMakeLists.txt - FINAL STATE

target_include_directories(orc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/public           # Public API only
    ${CMAKE_SOURCE_DIR}/orc/presenters/include  # Presenter headers
    ${CMAKE_BINARY_DIR}/generated            # Generated files
    # NO CORE DIRECTORIES
)

target_link_libraries(orc-gui PRIVATE
    orc-presenters   # Links to presenters (which privately link core)
    orc-public       # Public API
    orc-common       # Common types
    Qt6::Core
    Qt6::Widgets
    QtNodes
    # NO DIRECT orc-core LINK
)
```

**2. Presenter Layer Links Core Privately**

```cmake
# orc/presenters/CMakeLists.txt

target_link_libraries(orc-presenters
    PUBLIC
        orc-common   # Common types exposed to GUI
        orc-public   # Public API exposed to GUI
    PRIVATE
        orc-core     # Core hidden from GUI
)

target_include_directories(orc-presenters
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/orc/core/include
        ${CMAKE_SOURCE_DIR}/orc/core/analysis
)
```

**3. Core Headers Marked Private**

```cmake
# orc/core/CMakeLists.txt

target_include_directories(orc-core
    PRIVATE  # ← Not PUBLIC or INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/stages
)
```

### Compile-Time Validation

**Add compile guards to core headers:**

```cpp
// orc/core/include/project.h

#ifndef ORC_CORE_PROJECT_H
#define ORC_CORE_PROJECT_H

#if defined(ORC_GUI_BUILD) || defined(ORC_CLI_BUILD)
#error "GUI/CLI cannot include core internal headers. Use presenters or public API."
#endif

// ... rest of header
```

**Define build flags:**

```cmake
# orc/gui/CMakeLists.txt
target_compile_definitions(orc-gui PRIVATE ORC_GUI_BUILD)

# orc/cli/CMakeLists.txt  
target_compile_definitions(orc-cli PRIVATE ORC_CLI_BUILD)

# orc/presenters/CMakeLists.txt
# NO BUILD FLAG - allowed to see core
```

### Verification Script Enhancement

Update `orc/public/validate_mvp.sh`:

```bash
#!/bin/bash

# 1. Check source files for core includes
GUI_VIOLATIONS=$(grep -r '#include.*\.\./core\|#include.*core/' \
    --include='*.h' --include='*.cpp' \
    orc/gui/ orc/cli/ 2>/dev/null || true)

# 2. Check for forbidden core header names
FORBIDDEN_HEADERS="project\.h|preview_renderer\.h|stage_parameter\.h|\
project_to_dag\.h|dag_executor\.h|analysis_registry\.h"

HEADER_VIOLATIONS=$(grep -rE "#include.*($FORBIDDEN_HEADERS)" \
    --include='*.h' --include='*.cpp' \
    orc/gui/ orc/cli/ 2>/dev/null || true)

# 3. Verify CMakeLists doesn't expose core
CMAKE_VIOLATIONS=$(grep -E "orc/core" orc/gui/CMakeLists.txt orc/cli/CMakeLists.txt || true)

# Report all violations
if [ -n "$GUI_VIOLATIONS" ] || [ -n "$HEADER_VIOLATIONS" ] || [ -n "$CMAKE_VIOLATIONS" ]; then
    echo "❌ MVP VIOLATIONS DETECTED"
    exit 1
fi

echo "✅ MVP ARCHITECTURE VALIDATED"
exit 0
```

---

## Migration Patterns

### Pattern 1: Replace Direct Core Access with Presenter

**Before:**
```cpp
// gui/mainwindow.cpp
#include "project.h"

void MainWindow::someMethod() {
    orc::Project& project = guiProject_.coreProject();
    auto nodes = project.get_nodes();  // Direct core access
}
```

**After:**
```cpp
// gui/mainwindow.cpp
// No core includes

void MainWindow::someMethod() {
    auto nodes = projectPresenter_->getNodes();  // Via presenter
}
```

### Pattern 2: Replace Core Types with Public API Types

**Before:**
```cpp
// gui/fieldpreviewwidget.h
#include "preview_renderer.h"

class FieldPreviewWidget {
    void updatePreview(const orc::PreviewRenderResult& result);
};
```

**After:**
```cpp
// gui/fieldpreviewwidget.h
#include <orc_rendering.h>

class FieldPreviewWidget {
    void updatePreview(const orc::public_api::PreviewRenderResult& result);
};
```

### Pattern 3: Move Shared Types to Common

**Before:**
```cpp
// core/include/stage_parameter.h (private to core)
namespace orc {
    enum class ParameterType { INT32, DOUBLE, BOOL, STRING };
}

// gui/stageparameterdialog.h (includes core header!)
#include "stage_parameter.h"
```

**After:**
```cpp
// common/include/parameter_types.h (shared)
namespace orc {
    enum class ParameterType { INT32, DOUBLE, BOOL, STRING };
}

// core includes common/parameter_types.h
// gui includes common/parameter_types.h
// No dependency between core and gui
```

### Pattern 4: Wrap Complex Operations in Presenter

**Before:**
```cpp
// gui/guiproject.cpp
#include "project_to_dag.h"

void GUIProject::rebuildDAG() {
    dag_ = orc::project_to_dag(core_project_);  // Direct call
}
```

**After:**
```cpp
// gui/guiproject.cpp
// No core includes

void GUIProject::rebuildDAG() {
    dag_handle_ = presenter_->buildDAG();  // Presenter wraps complexity
}

// presenters/src/project_presenter.cpp
std::shared_ptr<void> ProjectPresenter::buildDAG() {
    #include "project_to_dag.h"  // OK - presenters can see core
    return orc::project_to_dag(project_);
}
```

---

## Validation & Testing

### Automated Validation

**1. Pre-Commit Hook**

Create `.git/hooks/pre-commit`:
```bash
#!/bin/bash
./orc/public/validate_mvp.sh || {
    echo "❌ MVP validation failed. Commit rejected."
    exit 1
}
```

**2. CI/CD Integration**

```yaml
# .github/workflows/mvp-validation.yml
name: MVP Architecture Validation

on: [push, pull_request]

jobs:
  validate-mvp:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Validate MVP Architecture
        run: bash orc/public/validate_mvp.sh
      - name: Build with strict mode
        run: |
          cmake -B build -DORC_STRICT_MVP=ON
          cmake --build build
```

**3. Compilation Test**

```bash
# Must succeed
cmake -B build -DORC_STRICT_MVP=ON
cmake --build build

# Must fail if core headers included from GUI
# (due to compile guards with #error)
```

### Manual Testing Checklist

After each phase, verify:

- [ ] Application builds successfully
- [ ] Application runs without crashes
- [ ] All dialogs open and function correctly
- [ ] Preview rendering works
- [ ] Analysis tools run successfully
- [ ] Project save/load works
- [ ] DAG operations work
- [ ] Parameter editing works
- [ ] No performance regression

### Regression Test Suite

Create automated tests for critical paths:

```cpp
// tests/mvp/test_gui_architecture.cpp

TEST(MVPArchitecture, GuiDoesNotIncludeCoreHeaders) {
    // This test ensures compilation enforcement
    // If GUI includes core headers, compile guards will fail this
    #ifdef ORC_GUI_BUILD
        // If this compiles, no core headers were included
        SUCCEED();
    #else
        FAIL() << "ORC_GUI_BUILD not defined";
    #endif
}

TEST(MVPArchitecture, PresentersProvideAllNeededOperations) {
    ProjectPresenter presenter;
    
    // Verify all operations exist and work
    presenter.createQuickProject(VideoFormat::NTSC, SourceType::Composite, {});
    presenter.buildDAG();
    presenter.getNodes();
    // ... all operations
}
```

---

## Success Criteria

### Quantitative Metrics

- [ ] **Zero** core header includes in GUI files (enforced by compiler)
- [ ] **Zero** core header includes in CLI files (enforced by compiler)
- [ ] **Zero** core directories in GUI/CLI CMakeLists.txt
- [ ] **Zero** `TODO(MVP)` comments
- [ ] **Zero** bridge libraries with special core access
- [ ] **100%** of validation script checks passing
- [ ] **< 5%** performance regression from baseline

### Qualitative Goals

- [ ] GUI developers cannot accidentally include core headers (compile error)
- [ ] All business logic lives in core or presenters
- [ ] Public API is complete and well-documented
- [ ] Presenters provide intuitive, high-level operations
- [ ] Code is more maintainable than before
- [ ] Architecture is self-documenting through structure

### Compiler Enforcement Verification

```bash
# Test 1: Try to include core header from GUI
echo '#include "project.h"' >> orc/gui/test.cpp
cmake --build build  # MUST FAIL with clear error

# Test 2: Verify CMakeLists doesn't expose core
grep "orc/core" orc/gui/CMakeLists.txt  # MUST return nothing

# Test 3: Run validation script
bash orc/public/validate_mvp.sh  # MUST pass

# Test 4: Build entire project
cmake -B build -DORC_STRICT_MVP=ON
cmake --build build -j$(nproc)  # MUST succeed
```

---

## Timeline & Milestones

### Week 1: Type Migration & Render Presenter
- **Day 1-2**: Phase 2.1 - Move types to common/public
- **Day 3-4**: Phase 2.2 - Enhance RenderPresenter
- **Day 5**: Testing and validation

**Milestone:** render_coordinator.cpp has zero core includes

### Week 2: Project & Analysis Presenters
- **Day 1-2**: Phase 2.3 - Enhance ProjectPresenter
- **Day 3**: Phase 2.4 - Analysis Presenter Enhancement
- **✅ Day 3**: Phase 2.5 - Parameter Dialog Migration (COMPLETE 2026-01-24)

**Milestone:** All parameter dialogs use common types ✅

### Week 3: Cleanup & Enforcement
- **Day 1**: Phase 2.6 - Preview Widget Migration (signal types to public API)
- **Day 2-3**: Phase 2.7 - Trigger Operation Migration (remove ld_sink_stage.h)
- **Day 4**: Phase 2.8 - Analysis bridge decision & implementation
- **Day 5**: Compiler enforcement setup and final validationODOs
- **Day 5**: Final validation and testing

**Milestone:** 100% MVP compliance, compiler enforced

---

## Risk Mitigation

### Risk: Breaking Existing Functionality

**Mitigation:**
- Comprehensive automated test suite before starting
- Phase-by-phase validation
- Keep old code in separate branch until verified
- User acceptance testing after each phase

### Risk: Performance Degradation

**Mitigation:**
- Profile before and after each phase
- Benchmark critical operations
- Optimize presenter layer if needed (inline small functions)
- Accept small overhead (<5%) for architectural benefits

### Risk: Incomplete Presenter API

**Mitigation:**
- Audit all GUI→Core interactions before starting
- Design complete presenter API upfront
- Mock/stub presenter methods early
- Add missing methods as discovered

### Risk: Analysis Bridge Complexity

**Mitigation:**
- Make explicit decision early (Option A/B/C)
- Don't get stuck on perfect solution
- Document tradeoffs clearly
- Can revisit later if needed

---

## Conclusion

This plan completes the MVP architecture migration through a systematic, phase-by-phase approach. Each phase builds on the previous, with clear deliverables and validation criteria.

**Key Principles:**
1. **Incremental Progress** - Each phase leaves the codebase in a working state
2. **Compiler Enforcement** - Not just script validation, but build-time guarantees
3. **Clear Boundaries** - No ambiguity about what belongs where
4. **Type Unification** - Single source of truth for shared types
5. **Complete Isolation** - Core is truly private, presenters are the bridge

**Next Step:** Begin Phase 2.1 - Type Migration to Common/Public

---

**Document Status:** Active Implementation Plan  
**Owner:** Development Team  
**Related:** [mvp-architecture-implementation-plan.md](mvp-architecture-implementation-plan.md), [mvp-architecture-violations.md](mvp-architecture-violations.md)
