#!/bin/bash
# validate_mvp.sh - Comprehensive MVP architecture validation
#
# This script validates that the MVP architecture separation is maintained:
# - GUI layer doesn't include core headers
# - CLI layer doesn't include core headers
# - Public API doesn't expose core namespaces
# - Presenter layer (when created) doesn't expose core to GUI

set -e

echo "=== MVP Architecture Validation ==="
echo ""

VIOLATIONS=0

# 1. Check GUI doesn't include core headers
echo "Checking GUI layer..."
GUI_VIOLATIONS=$(grep -r '#include.*"\.\./core' orc/gui/ 2>/dev/null || true)
if [ -n "$GUI_VIOLATIONS" ]; then
    echo "❌ FAIL: GUI includes core headers:"
    echo "$GUI_VIOLATIONS"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "✅ GUI layer clean (no core includes)"
fi
echo ""

# 2. Check CLI doesn't include core headers
echo "Checking CLI layer..."
CLI_VIOLATIONS=$(grep -r '#include.*"\.\./core' orc/cli/ 2>/dev/null || true)
if [ -n "$CLI_VIOLATIONS" ]; then
    echo "❌ FAIL: CLI includes core headers:"
    echo "$CLI_VIOLATIONS"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "✅ CLI layer clean (no core includes)"
fi
echo ""

# 3. Check public API doesn't expose core types directly
echo "Checking public API..."
if [ -d "orc/public" ]; then
    # Look for direct namespace orc usage in public headers (excluding orc::public_api)
    PUBLIC_LEAKS=$(grep -r 'namespace orc' orc/public/*.h 2>/dev/null | grep -v 'orc::public_api' || true)
    if [ -n "$PUBLIC_LEAKS" ]; then
        echo "⚠️  WARNING: Public API may expose core namespaces:"
        echo "$PUBLIC_LEAKS"
        # Warning only, not a hard failure for now
    else
        echo "✅ Public API isolated"
    fi
else
    echo "ℹ️  Public API not yet created (expected in Phase 1)"
fi
echo ""

# 4. Check presenters don't expose core to GUI (if presenter layer exists)
echo "Checking presenter layer..."
if [ -d "orc/presenters" ]; then
    PRESENTER_LEAKS=$(grep -r '#include.*core/' orc/presenters/include/ 2>/dev/null || true)
    if [ -n "$PRESENTER_LEAKS" ]; then
        echo "❌ FAIL: Presenter headers include core:"
        echo "$PRESENTER_LEAKS"
        VIOLATIONS=$((VIOLATIONS + 1))
    else
        echo "✅ Presenter layer clean"
    fi
else
    echo "ℹ️  Presenter layer not yet created (expected in Phase 2)"
fi
echo ""

# 5. Verify build system configuration
echo "Checking CMake configuration..."
if [ -f "orc/gui/CMakeLists.txt" ]; then
    if grep -q 'orc/core/include' orc/gui/CMakeLists.txt 2>/dev/null; then
        echo "❌ FAIL: GUI CMakeLists still includes core directories"
        VIOLATIONS=$((VIOLATIONS + 1))
    else
        echo "✅ Build system enforces separation"
    fi
else
    echo "⚠️  GUI CMakeLists.txt not found"
fi
echo ""

# 6. Verify common types are properly separated
echo "Checking common types..."
if [ -d "orc/common/include" ]; then
    COMMON_COUNT=$(ls -1 orc/common/include/*.h 2>/dev/null | wc -l)
    echo "✅ Common module exists with $COMMON_COUNT header(s)"
    
    # Check that core uses common types with angle brackets
    CORE_COMMON_USAGE=$(grep -r '#include <field_id.h>\|#include <node_id.h>\|#include <node_type.h>' orc/core/ 2>/dev/null | wc -l)
    if [ "$CORE_COMMON_USAGE" -gt 0 ]; then
        echo "✅ Core uses common types with angle brackets ($CORE_COMMON_USAGE occurrences)"
    else
        echo "⚠️  Core may not be using common types yet"
    fi
else
    echo "❌ FAIL: Common module not found"
    VIOLATIONS=$((VIOLATIONS + 1))
fi
echo ""

# Final summary
echo "=== Validation Summary ==="
if [ $VIOLATIONS -eq 0 ]; then
    echo "✅ All MVP validation checks passed!"
    exit 0
else
    echo "❌ Found $VIOLATIONS violation(s)"
    exit 1
fi
