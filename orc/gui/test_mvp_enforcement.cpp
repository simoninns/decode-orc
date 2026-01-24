// test_mvp_enforcement.cpp
// This file intentionally violates MVP architecture to verify compiler enforcement
// Expected: This file should FAIL to compile with a clear error message
// Usage: Uncomment one violation at a time to test enforcement

// =============================================================================
// VIOLATION TEST 1: Direct core header include
// =============================================================================
// Uncomment to test:
// #include "project.h"
// Expected error: "GUI code cannot include core/include/project.h. Use ProjectPresenter instead."

// =============================================================================
// VIOLATION TEST 2: Relative path to core
// =============================================================================
// Uncomment to test:
// #include "../core/include/preview_renderer.h"
// Expected error: "GUI code cannot include core/include/preview_renderer.h..."

// =============================================================================
// VIOLATION TEST 3: DAG executor access
// =============================================================================
// Uncomment to test:
// #include "dag_executor.h"
// Expected error: Compile guard should trigger

// =============================================================================
// VIOLATION TEST 4: Analysis registry access
// =============================================================================
// Uncomment to test:
// #include "analysis_registry.h"
// Expected error: Compile guard should trigger

// =============================================================================
// SUCCESS: This file compiles when all violations are commented out
// =============================================================================
void mvp_enforcement_test() {
    // This function exists only to create a valid compilation unit
    // The real test is whether uncommenting any violation above causes compilation to fail
}

// =============================================================================
// HOW TO USE THIS TEST:
// =============================================================================
// 1. Uncomment ONE violation at a time above
// 2. Run: cmake --build build
// 3. Verify build FAILS with descriptive error message
// 4. Re-comment the violation
// 5. Verify build SUCCEEDS
// 6. Repeat for each violation
//
// If ANY violation compiles successfully, the MVP enforcement is broken!
// =============================================================================
