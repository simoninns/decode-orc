#!/bin/bash
# validate_mvp.sh - Comprehensive MVP architecture validation
#
# This script validates that the MVP architecture separation is maintained:
# - GUI layer doesn't include core headers
# - CLI layer doesn't include core headers  
# - Public API doesn't expose core namespaces
# - Presenter layer doesn't expose core to GUI
# - CMake build system enforces architectural boundaries
#
# Exit codes:
#   0 = All validations passed
#   1 = Architecture violations found

set -e

echo "=== MVP Architecture Validation ==="
echo "Validating compile-time enforcement of MVP architecture boundaries"
echo ""

VIOLATIONS=0

# =============================================================================
# 1. Check GUI doesn't include core headers (relative paths)
# =============================================================================
echo "1. Checking GUI layer for relative core includes..."
# Exclude orc-gui-vectorscope library (documented exception for real-time visualization)
GUI_VIOLATIONS=$(grep -r '#include.*\.\./core\|#include.*core/' \
    --include='*.h' --include='*.cpp' \
    --exclude-dir='analysis' \
    orc/gui/ 2>/dev/null || true)

if [ -n "$GUI_VIOLATIONS" ]; then
    echo "❌ FAIL: GUI includes core headers:"
    echo "$GUI_VIOLATIONS"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "✅ GUI layer clean (no relative core includes)"
fi
echo ""

# =============================================================================
# 2. Check for forbidden core header names in GUI/CLI HEADERS (not .cpp files)
# =============================================================================
echo "2. Checking for forbidden core headers in header files..."
# We check only .h files because .cpp files may temporarily include core headers
# The compile guards will catch them during build
# Note: We exclude guiproject.h since it's a GUI wrapper, not a core header
FORBIDDEN_HEADERS='project\.h"|preview_renderer\.h"|stage_parameter\.h"|project_to_dag\.h"|dag_executor\.h"|analysis_registry\.h"'

HEADER_VIOLATIONS=$(grep -rE "#include\s+\"($FORBIDDEN_HEADERS)" \
    --include='*.h' \
    --exclude-dir='analysis' \
    orc/gui/ orc/cli/ 2>/dev/null || true)

if [ -n "$HEADER_VIOLATIONS" ]; then
    echo "❌ FAIL: GUI/CLI headers include forbidden core headers:"
    echo "$HEADER_VIOLATIONS"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "✅ No forbidden core headers in GUI/CLI header files"
    echo "   (Implementation files will be caught by compile guards)"
fi
echo ""

# =============================================================================
# 3. Verify CMakeLists doesn't expose core directories
# =============================================================================
echo "3. Checking CMake configuration..."
CMAKE_VIOLATIONS=""

if [ -f "orc/gui/CMakeLists.txt" ]; then
    # Check for any orc/core paths in target_include_directories for orc-gui (not orc-gui-vectorscope)
    # We use awk to extract only the orc-gui target's include directories
    ORC_GUI_INCLUDES=$(awk '/target_include_directories\(orc-gui PRIVATE/,/\)/' orc/gui/CMakeLists.txt)
    
    if echo "$ORC_GUI_INCLUDES" | grep -q 'orc/core'; then
        CMAKE_VIOLATIONS="${CMAKE_VIOLATIONS}GUI target_include_directories exposes core directories\n"
    fi
    
    # Check for direct orc-core link in orc-gui target (should only link via presenters)
    ORC_GUI_LINKS=$(awk '/target_link_libraries\(orc-gui PRIVATE/,/\)/' orc/gui/CMakeLists.txt | grep -v '#')
    
    if echo "$ORC_GUI_LINKS" | grep -qE '^\s*orc-core\s*$|^\s*orc-core\s'; then
        CMAKE_VIOLATIONS="${CMAKE_VIOLATIONS}GUI target_link_libraries links orc-core directly\n"
    fi
fi

if [ -f "orc/cli/CMakeLists.txt" ]; then
    # CLI is currently allowed core access (documented exception)
    # We just verify it has the ORC_CLI_BUILD flag
    if ! grep -q 'ORC_CLI_BUILD' orc/cli/CMakeLists.txt; then
        echo "⚠️  WARNING: CLI CMakeLists.txt missing ORC_CLI_BUILD flag"
    fi
fi

if [ -n "$CMAKE_VIOLATIONS" ]; then
    echo "❌ FAIL: CMake violations in orc-gui target:"
    echo -e "$CMAKE_VIOLATIONS"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "✅ Build system enforces MVP architecture (orc-gui target)"
    echo "   Note: orc-gui-vectorscope is allowed core access (documented exception)"
fi
echo ""

# =============================================================================
# 4. Check public API doesn't expose core types directly
# =============================================================================
echo "4. Checking public API isolation..."
if [ -d "orc/public" ]; then
    # Look for direct namespace orc usage in public headers (excluding orc::public_api)
    PUBLIC_LEAKS=$(grep -r 'namespace orc' orc/public/*.h 2>/dev/null | grep -v 'orc::public_api' || true)
    if [ -n "$PUBLIC_LEAKS" ]; then
        echo "⚠️  WARNING: Public API may expose core namespaces:"
        echo "$PUBLIC_LEAKS"
        # Warning only, not a hard failure for now
    else
        echo "✅ Public API properly isolated"
    fi
else
    echo "⚠️  Public API directory not found"
fi
echo ""

# =============================================================================
# 5. Check presenters don't expose core in public headers
# =============================================================================
echo "5. Checking presenter layer..."
if [ -d "orc/presenters/include" ]; then
    PRESENTER_LEAKS=$(grep -r '#include.*core/' orc/presenters/include/ 2>/dev/null || true)
    if [ -n "$PRESENTER_LEAKS" ]; then
        echo "❌ FAIL: Presenter public headers include core:"
        echo "$PRESENTER_LEAKS"
        VIOLATIONS=$((VIOLATIONS + 1))
    else
        echo "✅ Presenter public headers are clean"
    fi
    
    # Check that presenters link core PRIVATELY in CMakeLists
    if [ -f "orc/presenters/CMakeLists.txt" ]; then
        # Extract the target_link_libraries block and check if orc-core is under PRIVATE
        PRESENTER_LINKS=$(awk '/target_link_libraries\(orc-presenters/,/^\)/' orc/presenters/CMakeLists.txt)
        
        if echo "$PRESENTER_LINKS" | grep -A20 'PRIVATE' | grep -q 'orc-core'; then
            echo "✅ Presenters link core privately (hidden from GUI)"
        else
            echo "⚠️  WARNING: Presenters may not link core privately"
        fi
    fi
else
    echo "⚠️  Presenter layer not found"
fi
echo ""

# =============================================================================
# 6. Verify common types module exists and is used
# =============================================================================
echo "6. Checking common types module..."
if [ -d "orc/common/include" ]; then
    COMMON_COUNT=$(ls -1 orc/common/include/*.h 2>/dev/null | wc -l)
    echo "✅ Common module exists with $COMMON_COUNT header(s)"
    
    # Check that core uses common types with angle brackets
    CORE_COMMON_USAGE=$(grep -r '#include <field_id.h>\|#include <node_id.h>\|#include <node_type.h>\|#include <parameter_types.h>' orc/core/ 2>/dev/null | wc -l)
    if [ "$CORE_COMMON_USAGE" -gt 0 ]; then
        echo "✅ Core uses common types ($CORE_COMMON_USAGE occurrences)"
    else
        echo "⚠️  Core may not be using common types yet"
    fi
else
    echo "❌ FAIL: Common module not found"
    VIOLATIONS=$((VIOLATIONS + 1))
fi
echo ""

# =============================================================================
# 7. Verify compile guards are present in key core headers
# =============================================================================
echo "7. Checking compile guards in core headers..."
GUARD_CHECK_PASSED=true

# List of headers that should have MVP guards
declare -a GUARDED_HEADERS=(
    "orc/core/include/project.h"
    "orc/core/include/preview_renderer.h"
    "orc/core/include/project_to_dag.h"
    "orc/core/include/stage_parameter.h"
    "orc/core/analysis/analysis_registry.h"
)

for HEADER in "${GUARDED_HEADERS[@]}"; do
    if [ -f "$HEADER" ]; then
        if grep -q 'ORC_GUI_BUILD' "$HEADER"; then
            echo "  ✅ $HEADER has MVP guard"
        else
            echo "  ❌ $HEADER missing MVP guard"
            GUARD_CHECK_PASSED=false
        fi
    fi
done

if $GUARD_CHECK_PASSED; then
    echo "✅ All key headers have compile guards"
else
    echo "❌ FAIL: Some headers missing compile guards"
    VIOLATIONS=$((VIOLATIONS + 1))
fi
echo ""

# =============================================================================
# 8. Verify build flags are set correctly
# =============================================================================
echo "8. Checking build flag configuration..."
FLAG_CHECK_PASSED=true

# GUI should have ORC_GUI_BUILD
if [ -f "orc/gui/CMakeLists.txt" ]; then
    if grep -q 'ORC_GUI_BUILD' orc/gui/CMakeLists.txt; then
        echo "  ✅ GUI has ORC_GUI_BUILD flag"
    else
        echo "  ❌ GUI missing ORC_GUI_BUILD flag"
        FLAG_CHECK_PASSED=false
    fi
fi

# CLI should have ORC_CLI_BUILD (even though it's allowed core access)
if [ -f "orc/cli/CMakeLists.txt" ]; then
    if grep -q 'ORC_CLI_BUILD' orc/cli/CMakeLists.txt; then
        echo "  ✅ CLI has ORC_CLI_BUILD flag"
    else
        echo "  ⚠️  CLI missing ORC_CLI_BUILD flag (acceptable for now)"
    fi
fi

if $FLAG_CHECK_PASSED; then
    echo "✅ Build flags configured correctly"
else
    echo "❌ FAIL: Build flags not properly configured"
    VIOLATIONS=$((VIOLATIONS + 1))
fi
echo ""

# =============================================================================
# Final Summary
# =============================================================================
echo "=== Validation Summary ==="
echo ""
if [ $VIOLATIONS -eq 0 ]; then
    echo "✅ ✅ ✅ ALL MVP VALIDATION CHECKS PASSED! ✅ ✅ ✅"
    echo ""
    echo "The MVP architecture is properly enforced:"
    echo "  • GUI code cannot include core headers (compile-time error)"
    echo "  • Build system prevents core access via include paths"
    echo "  • All communication happens through presenters"
    echo "  • Compile guards enforce architectural boundaries"
    echo ""
    exit 0
else
    echo "❌ ❌ ❌ FOUND $VIOLATIONS VIOLATION(S) ❌ ❌ ❌"
    echo ""
    echo "Please fix the violations listed above before committing."
    echo ""
    exit 1
fi
