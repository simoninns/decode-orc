# MVP Architecture Validation

This document explains how to validate the MVP (Model-View-Presenter) architecture enforcement in the decode-orc project.

## Quick Start

### Manual Validation

```bash
# Run all checks (interface + compiler tests)
./check_mvp_architecture.sh

# Fast mode (skip compiler tests, ~1 second)
./check_mvp_architecture.sh --skip-compiler-tests
```

### Via CMake

```bash
# Full validation
cmake --build build --target check-mvp

# Fast validation (skip compiler tests)
cmake --build build --target check-mvp-fast
```

## What Gets Checked

The script performs three types of validation:

### 1. **Interface Leakage Detection** ❌ Currently Failing
Checks presenter headers (`orc/presenters/include/*.h`) for core types exposed in public/protected APIs.

**Violations Found:**
- `orc::Project*` pointers in presenter constructors
- `orc::DAG` shared pointers in presenter methods
- `orc::VideoFieldRepresentation` in public interfaces
- `orc::ObservationContext` references

### 2. **GUI Layer Validation** ⚠️ Has Warnings
Checks GUI headers (`orc/gui/*.h`) for references to core types.

**Current Issues:**
- Forward declarations of core types in GUI headers
- Direct usage of core type pointers

### 3. **Compiler Enforcement** ✅ Passing
Tests that compile-time guards prevent direct inclusion of core headers.

**Verified:**
- ✅ GUI code with `-DORC_GUI_BUILD` cannot `#include` core headers
- ✅ Public API headers compile successfully
- ✅ Error messages are clear and actionable

## How the Violations Happen

Despite compile guards preventing direct `#include` of core headers, violations occur through:

1. **Forward Declarations in Presenter Headers**
   ```cpp
   namespace orc { class Project; }  // No #include needed
   
   class ProjectPresenter {
       orc::Project* getCoreProject();  // ❌ Exposes core type
   };
   ```

2. **Opaque Pointers Passed to GUI**
   ```cpp
   // GUI can store and pass core types without knowing their definition
   auto dag = presenter->getDAG();  // Returns std::shared_ptr<orc::DAG>
   ```

3. **Template Instantiation**
   ```cpp
   std::function<std::shared_ptr<const orc::DAG>()>  // ❌ Core type in template
   ```

## How to Fix Violations

### Option 1: Use Opaque Handles
```cpp
// Instead of:
std::shared_ptr<orc::DAG> getDAG();

// Use:
std::shared_ptr<void> getDAG();  // Opaque handle
```

### Option 2: Create View Models
```cpp
// Create wrapper type in orc/presenters/include/project_view_models.h
struct ProjectHandle {
    void* opaque_ptr;
};

// Use in presenter:
ProjectHandle getProject();
```

### Option 3: Hide Implementation with Pimpl
```cpp
class ProjectPresenter {
    class Impl;  // Forward declare implementation
    std::unique_ptr<Impl> impl_;  // No core types in header
};
```

### Option 4: Add Presenter Methods
```cpp
// Instead of: getCoreProject() + direct access
auto* project = presenter->getCoreProject();
project->doSomething();

// Use: presenter methods
presenter->performOperation();  // Delegates to core internally
```

## Automation Options

### Option 1: Pre-Commit Hook (Recommended)
```bash
# Create .git/hooks/pre-commit
#!/bin/bash
./check_mvp_architecture.sh --skip-compiler-tests
```

### Option 2: Automatic on Build
Uncomment in `orc/CMakeLists.txt`:
```cmake
# Makes every build run the check (adds ~1 second)
if(BUILD_GUI)
    add_dependencies(orc-gui check-mvp-fast)
endif()
```

### Option 3: CI/CD Integration
Already configured in `.github/workflows/mvp-validation.yml`

Runs on:
- Every pull request touching `orc/presenters/`, `orc/gui/`, `orc/cli/`
- Pushes to `main` and `develop` branches

## Current Status

| Check | Status | Count |
|-------|--------|-------|
| Interface Leakage | ❌ Failing | ~19 violations |
| GUI Warnings | ⚠️ Warnings | ~3 warnings |
| Compiler Guards | ✅ Passing | 0 issues |

## Enforcement Strategy

**Two-Layer Defense:**

1. **Compile Guards** (Runtime) ✅
   - Prevents `#include` of core headers
   - Works perfectly for direct inclusion
   - Cannot prevent forward declarations

2. **Static Analysis** (This Script) ⚠️
   - Detects forward declarations in public APIs
   - Catches indirect type leakage
   - Currently identifies violations but doesn't block builds

**To fully enforce:** Uncomment the `add_dependencies()` lines in `orc/CMakeLists.txt` to make builds fail on violations.

## Further Reading

- `test_mvp_compiler_enforcement.sh` - Original compile guard tests (now integrated)
- `orc/presenters/README.md` - Presenter layer architecture
- `docs/mvp-architecture.md` - Full MVP design documentation
