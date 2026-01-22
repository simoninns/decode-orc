# MVP Architecture Violations

**Date:** 2026-01-22  
**Status:** Identified - Not yet resolved  
**Severity:** High - Fundamental architectural issue

## Summary

The GUI layer directly includes and uses core implementation headers, violating the intended MVP (Model-View-Presenter) architecture. The build system explicitly grants GUI access to all core directories, making it impossible to enforce separation at compile time.

## Violations Found

### 1. Build System Explicitly Grants Core Access

**File:** [orc/gui/CMakeLists.txt](../orc/gui/CMakeLists.txt)

Lines 136-143 originally granted GUI full access to core:

```cmake
target_include_directories(orc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/core/include
    ${CMAKE_SOURCE_DIR}/orc/core/stages
    ${CMAKE_SOURCE_DIR}/orc/core/analysis
    ${CMAKE_SOURCE_DIR}/orc/core/hints
    ${CMAKE_SOURCE_DIR}/orc/core/util
    # ... more core paths
)
```

**Impact:** No compile-time enforcement of MVP separation. GUI can include any core header.

### 2. GUI Headers Directly Include Core Implementation

**Total Violations:** 64 direct includes of core headers from GUI code

#### Header Files (20+ violations):
- [orc/gui/hintsdialog.h](../orc/gui/hintsdialog.h) - 5 core includes (hints/, tbc_metadata.h)
- [orc/gui/qualitymetricsdialog.h](../orc/gui/qualitymetricsdialog.h) - 2 includes (field_id.h, node_id.h)
- [orc/gui/previewdialog.h](../orc/gui/previewdialog.h) - 2 includes (preview_renderer.h, tbc_metadata.h)
- [orc/gui/render_coordinator.h](../orc/gui/render_coordinator.h) - 4 includes (node_id.h, dropout/snr/burst analysis types)
- [orc/gui/mainwindow.h](../orc/gui/mainwindow.h) - 2 includes (node_id.h - duplicated)
- [orc/gui/snranalysisdialog.h](../orc/gui/snranalysisdialog.h) - 1 include (snr_analysis_types.h)
- [orc/gui/dropoutanalysisdialog.h](../orc/gui/dropoutanalysisdialog.h) - 1 include (dropout_analysis_types.h)
- [orc/gui/dropout_editor_dialog.h](../orc/gui/dropout_editor_dialog.h) - 2 includes (field_id.h, dropout_decision.h, video_field_representation.h, dropout_map_stage.h)
- [orc/gui/orcgraphmodel.h](../orc/gui/orcgraphmodel.h) - 1 include (node_id.h)
- [orc/gui/orcgraphicsscene.h](../orc/gui/orcgraphicsscene.h) - 1 include (node_id.h)
- [orc/gui/inspection_dialog.h](../orc/gui/inspection_dialog.h) - 1 include (stage.h)
- [orc/gui/node_type_helper.h](../orc/gui/node_type_helper.h) - 1 include (node_type.h)
- [orc/gui/vbidialog.h](../orc/gui/vbidialog.h) - 1 include (vbi_decoder.h)

#### Source Files (20+ violations):
- [orc/gui/mainwindow.cpp](../orc/gui/mainwindow.cpp) - 11 core includes (preview_renderer, vbi_decoder, dag_field_renderer, tbc_metadata, stage_registry, node_type, dag_executor, project_to_dag, chroma_sink_stage, analysis_registry, analysis_context)
- [orc/gui/render_coordinator.cpp](../orc/gui/render_coordinator.cpp) - 1 include (vbi_decoder.h)
- [orc/gui/ntscobserverdialog.cpp](../orc/gui/ntscobserverdialog.cpp) - 2 includes (observation_context.h, field_id.h)
- [orc/gui/qualitymetricsdialog.cpp](../orc/gui/qualitymetricsdialog.cpp) - 3 includes (video_field_representation.h, observation_context.h, node_id.h)
- [orc/gui/orcgraphicsscene.cpp](../orc/gui/orcgraphicsscene.cpp) - 4 includes (node_type.h, project.h, stage_registry.h, analysis_registry.h)
- [orc/gui/orcgraphview.cpp](../orc/gui/orcgraphview.cpp) - 1 include (project.h)
- [orc/gui/orcgraphmodel.cpp](../orc/gui/orcgraphmodel.cpp) - 1 include (project.h)

### 3. Business Logic in GUI

Several GUI components contain business logic that should be in the core:

#### [orc/gui/mainwindow.cpp](../orc/gui/mainwindow.cpp)
- `quickProject()` - Creates project structure with hardcoded stage selection logic
- Stage filtering based on video format (lines deciding which stages to show)
- Direct manipulation of DAG structure

#### [orc/gui/render_coordinator.cpp](../orc/gui/render_coordinator.cpp)
- Complex rendering orchestration that duplicates core logic
- Direct access to analysis sinks and their internal data structures

#### [orc/gui/dropout_editor_dialog.cpp](../orc/gui/dropout_editor_dialog.cpp)
- Dropout region rendering logic
- Decision-making about dropout correction strategies

## Root Cause Analysis

### Type Definition Conflicts

The core has full implementations of fundamental types:
- `orc::FieldID` (class with methods in [orc/core/include/field_id.h](../orc/core/include/field_id.h))
- `orc::NodeID` (class in [orc/core/include/node_id.h](../orc/core/include/node_id.h))
- `orc::NodeType` (enum in [orc/core/include/node_type.h](../orc/core/include/node_type.h))

Any public API that redefines these types causes compilation conflicts when the implementation bridge needs to see both.

### Missing Public API Layer

The [orc/public/](../orc/public/) directory was empty. There was no facade over core implementation, so GUI had no choice but to include core headers directly.

## Attempted Fixes

### 1. Created Public API Layer

Created 7 public API headers in [orc/public/](../orc/public/):
- [orc_api.h](../orc/public/orc_api.h) - Main facade header
- [orc_types.h](../orc/public/orc_types.h) - Type definitions
- [orc_project.h](../orc/public/orc_project.h) - Project management API
- [orc_rendering.h](../orc/public/orc_rendering.h) - Rendering API
- [orc_stages.h](../orc/public/orc_stages.h) - Stage registry API
- [orc_analysis.h](../orc/public/orc_analysis.h) - Analysis API
- [orc_logging.h](../orc/public/orc_logging.h) - Logging API
- [orc_metadata.h](../orc/public/orc_metadata.h) - Metadata API

### 2. Modified Build System

Changed [orc/gui/CMakeLists.txt](../orc/gui/CMakeLists.txt) to only expose public API:
```cmake
target_include_directories(orc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/public  # ONLY public API
)
```

### 3. Started GUI Migration

Successfully migrated 3 files to use public API:
- [orc/gui/guiproject.h](../orc/gui/guiproject.h)
- [orc/gui/guiproject.cpp](../orc/gui/guiproject.cpp)
- [orc/gui/logging.h](../orc/gui/logging.h)

### 4. Hit Type Conflict Issues

When creating [orc/core/public_api_impl.cpp](../orc/core/public_api_impl.cpp) as the bridge between public API and core, encountered fundamental conflicts:
- Public API defines `FieldID` as simple struct
- Core defines `FieldID` as complex class with methods
- Cannot include both definitions in same translation unit
- Compilation fails with "multiple definition" errors

## Recommended Solution

### Phase 1: Type Unification

**Option A: Core Types as Source of Truth**
1. Remove type definitions from [orc/public/orc_types.h](../orc/public/orc_types.h)
2. Move core types ([field_id.h](../orc/core/include/field_id.h), [node_id.h](../orc/core/include/node_id.h), etc.) to a shared location like `orc/common/`
3. Both core and public API include from `orc/common/`
4. Public API becomes pure interface/facade with no type redefinitions

**Option B: Public Types as Contract**
1. Simplify core's `FieldID`, `NodeID` to match public API simple structs
2. Move complex functionality to helper classes
3. Public API types become the contract
4. Core implementation conforms to public types

### Phase 2: Implement Public API

1. Complete implementation of all functions in [orc/core/public_api_impl.cpp](../orc/core/public_api_impl.cpp)
2. Ensure no type conflicts between public and core headers
3. Public API implementation file is the ONLY place that sees both public and core headers

### Phase 3: Migrate GUI

1. Systematically replace all `#include "../core/*"` with `#include <orc_api.h>`
2. Update GUI code to use public API functions instead of direct core access
3. Remove business logic from GUI (move to core or presenter layer)

### Phase 4: Refactor Business Logic

Move business logic out of GUI:
- `mainwindow.cpp::quickProject()` → core project template system
- Stage filtering → core configuration system  
- Dropout rendering logic → core analysis system
- Direct DAG manipulation → public project API

## Validation

Created [orc/public/validate_mvp.sh](../orc/public/validate_mvp.sh) script that checks:
1. GUI headers don't include core headers
2. GUI source files don't include core headers
3. Only public API is visible to GUI
4. Public API implementation doesn't leak core types

## Impact Assessment

- **Build Time:** Currently fails due to type conflicts in public_api_impl.cpp
- **Runtime:** Not tested (cannot compile)
- **Code Quality:** Improves separation of concerns once complete
- **Maintainability:** Significantly improved with proper MVP enforcement
- **Testing:** Easier to unit test GUI separately from core

## Dependencies

- All GUI files need migration
- Core types need architectural decision (Option A or B above)
- Public API implementation needs completion
- Build system changes are complete

## Next Steps

1. **Decision Required:** Choose Option A or B for type unification
2. Complete public API type definitions consistently
3. Implement all public API functions in public_api_impl.cpp
4. Resume GUI migration file-by-file
5. Move business logic from GUI to core
6. Achieve clean MVP architecture

## References

- [README.md](../orc/public/README.md) - Public API documentation
- [MIGRATION.md](../orc/public/MIGRATION.md) - Migration guide (if exists)
- [validate_mvp.sh](../orc/public/validate_mvp.sh) - Validation script
- Original review request: Code review for MVP compliance and business logic separation

---

**Note:** This represents ~40% completion of the MVP migration. The infrastructure is in place but type conflicts prevent compilation. A clear architectural decision on type ownership is needed before proceeding.
