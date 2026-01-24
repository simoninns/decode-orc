#!/bin/bash
# test_mvp_compiler_enforcement.sh
# Tests that the compiler actually catches MVP violations

set -e

echo "=== Testing MVP Compiler Enforcement ==="
echo ""

# Test 1: Try to compile a file that includes core headers with ORC_GUI_BUILD defined
echo "TEST 1: Attempting to compile code that includes core header with ORC_GUI_BUILD flag"
echo "Expected: Compiler should reject with error message"
echo ""

cat > /tmp/test_mvp_violation.cpp << 'EOF'
#include "project.h"
void test_function() {}
EOF

if g++ -c /tmp/test_mvp_violation.cpp \
    -I orc/core/include \
    -I orc/common/include \
    -DORC_GUI_BUILD \
    2>&1 | grep -q "error.*GUI code cannot include"; then
    echo "✅ PASS: Compiler correctly rejected core/include/project.h with ORC_GUI_BUILD flag"
else
    echo "❌ FAIL: Compiler did not catch project.h inclusion!"
    exit 1
fi

echo ""

# Test 2: Try preview_renderer.h
echo "TEST 2: Attempting to compile code that includes preview_renderer.h with ORC_GUI_BUILD flag"
echo ""

cat > /tmp/test_mvp_violation2.cpp << 'EOF'
#include "preview_renderer.h"
void test_function() {}
EOF

if g++ -c /tmp/test_mvp_violation2.cpp \
    -I orc/core/include \
    -I orc/common/include \
    -I orc/public \
    -DORC_GUI_BUILD \
    2>&1 | grep -q "error.*GUI code cannot include"; then
    echo "✅ PASS: Compiler correctly rejected core/include/preview_renderer.h with ORC_GUI_BUILD flag"
else
    echo "❌ FAIL: Compiler did not catch preview_renderer.h inclusion!"
    exit 1
fi

echo ""

# Test 3: Verify GUI can successfully compile without core includes
echo "TEST 3: Verifying GUI code can compile with public API"
echo ""

cat > /tmp/test_mvp_valid.cpp << 'EOF'
#include <orc_rendering.h>
#include <parameter_types.h>
void test_function() {
    orc::public_api::PreviewImage img;
    orc::ParameterValue param = std::string("test");
}
EOF

if g++ -c /tmp/test_mvp_valid.cpp \
    -I orc/public \
    -I orc/common/include \
    -DORC_GUI_BUILD \
    -std=c++17 \
    2>&1 > /tmp/compile_output.txt; then
    echo "✅ PASS: GUI code compiles successfully using public API and common types"
else
    echo "❌ FAIL: Valid GUI code failed to compile"
    cat /tmp/compile_output.txt
    exit 1
fi

# Cleanup
rm -f /tmp/test_mvp_*.cpp /tmp/compile_output.txt

echo ""
echo "=== All Compiler Enforcement Tests Passed ==="
echo ""
echo "Summary:"
echo "  ✅ Compile guards prevent core header inclusion in GUI"
echo "  ✅ Error messages are clear and actionable"
echo "  ✅ Public API and common types are accessible"
echo ""
echo "The MVP architecture is enforced at compile time."
