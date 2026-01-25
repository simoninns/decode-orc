#!/bin/bash
# check_mvp_architecture.sh
# Comprehensive MVP architecture validation
# 
# This script performs three types of checks:
# 1. Interface Leakage: Detects core types exposed in presenter public APIs
# 2. Compiler Guards: Verifies compile-time enforcement prevents direct core includes
# 3. GUI Violations: Checks GUI code doesn't reference core types
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TOTAL_VIOLATIONS=0
SKIP_COMPILER_TESTS=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-compiler-tests)
            SKIP_COMPILER_TESTS=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --skip-compiler-tests  Skip compiler enforcement tests (faster)"
            echo "  --help, -h             Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo "============================================================================="
echo "  MVP Architecture Validation"
echo "============================================================================="
echo ""

# =============================================================================
# CHECK 1: Interface Leakage Detection
# =============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "CHECK 1: Interface Leakage (Core types in presenter public APIs)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Core types that should NEVER appear in presenter public interfaces
FORBIDDEN_CORE_TYPES=(
    "orc::Project"
    "orc::DAG"
    "orc::PreviewRenderer"
    "orc::TBCReader"
    "orc::VideoFieldRepresentation"
    "orc::Artifact"
    "orc::DAGNode"
    "orc::ObservationContext"
    "orc::Stage"
)

PRESENTER_VIOLATIONS=0
PRESENTER_HEADERS="orc/presenters/include/*.h"

for header in $PRESENTER_HEADERS; do
    if [[ ! -f "$header" ]]; then
        continue
    fi
    
    # Extract public/protected sections (exclude private/implementation)
    PUBLIC_API=$(awk '
        /^public:|^protected:/ { in_public=1; next }
        /^private:/ { in_public=0; next }
        /^class.*{/ { in_public=1 }
        in_public { print }
    ' "$header")
    
    for core_type in "${FORBIDDEN_CORE_TYPES[@]}"; do
        if echo "$PUBLIC_API" | grep -q "$core_type"; then
            matches=$(grep -n "$core_type" "$header" | grep -v "Forward\|forward\|namespace orc")
            if [[ -n "$matches" ]]; then
                echo -e "${RED}❌ VIOLATION${NC} in $header:"
                echo "   Core type '$core_type' exposed in public interface:"
                echo "$matches" | sed 's/^/     /'
                echo ""
                PRESENTER_VIOLATIONS=$((PRESENTER_VIOLATIONS + 1))
            fi
        fi
    done
done

if [[ $PRESENTER_VIOLATIONS -eq 0 ]]; then
    echo -e "${GREEN}✅ PASS${NC}: No core types exposed in presenter interfaces"
else
    echo -e "${RED}✗ FAIL${NC}: Found $PRESENTER_VIOLATIONS presenter interface violation(s)"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + PRESENTER_VIOLATIONS))
fi
echo ""

# =============================================================================
# CHECK 2: GUI Layer Violations
# =============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "CHECK 2: GUI Layer (Core type references in GUI code)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

GUI_VIOLATIONS=0
GUI_HEADERS="orc/gui/*.h"

for header in $GUI_HEADERS; do
    if [[ ! -f "$header" ]]; then
        continue
    fi
    
    for core_type in "${FORBIDDEN_CORE_TYPES[@]}"; do
        # In GUI, even forward declarations are suspicious
        matches=$(grep -n "$core_type" "$header" | grep -v "^[[:space:]]*//")
        if [[ -n "$matches" ]]; then
            echo -e "${YELLOW}⚠️  WARNING${NC} in $header:"
            echo "   Core type '$core_type' referenced:"
            echo "$matches" | sed 's/^/     /'
            echo ""
            GUI_VIOLATIONS=$((GUI_VIOLATIONS + 1))
        fi
    done
done

if [[ $GUI_VIOLATIONS -eq 0 ]]; then
    echo -e "${GREEN}✅ PASS${NC}: No core types referenced in GUI headers"
else
    echo -e "${YELLOW}⚠ WARNING${NC}: Found $GUI_VIOLATIONS GUI header warning(s)"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + GUI_VIOLATIONS))
fi
echo ""

# =============================================================================
# CHECK 3: Compiler Enforcement Tests
# =============================================================================
if [[ $SKIP_COMPILER_TESTS -eq 0 ]]; then
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "CHECK 3: Compiler Enforcement (Compile guards prevent direct includes)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    
    COMPILER_FAILURES=0
    
    # Test 3.1: Core header inclusion should fail with ORC_GUI_BUILD
    echo "Test 3.1: Verify project.h rejected with ORC_GUI_BUILD..."
    cat > /tmp/test_mvp_violation.cpp << 'EOF'
#include "project.h"
void test_function() {}
EOF
    
    if g++ -c /tmp/test_mvp_violation.cpp \
        -I orc/core/include \
        -I orc/common/include \
        -DORC_GUI_BUILD \
        2>&1 | grep -q "error.*GUI code cannot include"; then
        echo -e "  ${GREEN}✅ PASS${NC}: Compiler correctly rejected project.h"
    else
        echo -e "  ${RED}❌ FAIL${NC}: Compiler did not catch project.h inclusion!"
        COMPILER_FAILURES=$((COMPILER_FAILURES + 1))
    fi
    
    # Test 3.2: Preview renderer header should also fail
    echo "Test 3.2: Verify preview_renderer.h rejected with ORC_GUI_BUILD..."
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
        echo -e "  ${GREEN}✅ PASS${NC}: Compiler correctly rejected preview_renderer.h"
    else
        echo -e "  ${RED}❌ FAIL${NC}: Compiler did not catch preview_renderer.h inclusion!"
        COMPILER_FAILURES=$((COMPILER_FAILURES + 1))
    fi
    
    # Test 3.3: Public API should compile successfully
    echo "Test 3.3: Verify public API compiles with ORC_GUI_BUILD..."
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
        echo -e "  ${GREEN}✅ PASS${NC}: Public API compiles successfully"
    else
        echo -e "  ${RED}❌ FAIL${NC}: Valid GUI code failed to compile"
        cat /tmp/compile_output.txt
        COMPILER_FAILURES=$((COMPILER_FAILURES + 1))
    fi
    
    # Cleanup
    rm -f /tmp/test_mvp_*.cpp /tmp/compile_output.txt
    
    if [[ $COMPILER_FAILURES -eq 0 ]]; then
        echo ""
        echo -e "${GREEN}✅ PASS${NC}: All compiler enforcement tests passed"
    else
        echo ""
        echo -e "${RED}✗ FAIL${NC}: $COMPILER_FAILURES compiler test(s) failed"
        TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + COMPILER_FAILURES))
    fi
    echo ""
else
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "CHECK 3: Compiler Enforcement (SKIPPED)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
fi

# =============================================================================
# Summary
# =============================================================================
echo "============================================================================="
echo "  Summary"
echo "============================================================================="
echo ""

if [[ $TOTAL_VIOLATIONS -eq 0 ]]; then
    echo -e "${GREEN}✅ ALL CHECKS PASSED${NC}"
    echo ""
    echo "MVP architecture is properly enforced:"
    echo "  ✓ Presenter APIs properly encapsulate core types"
    echo "  ✓ GUI code doesn't reference core types"
    if [[ $SKIP_COMPILER_TESTS -eq 0 ]]; then
        echo "  ✓ Compile guards prevent direct core header inclusion"
    fi
    echo ""
    exit 0
else
    echo -e "${RED}✗ VIOLATIONS DETECTED${NC}"
    echo ""
    echo "Found $TOTAL_VIOLATIONS total violation(s)"
    echo ""
    echo "Recommended fixes:"
    echo "  1. Use std::shared_ptr<void> for opaque handles"
    echo "  2. Add presenter methods instead of exposing core pointers"
    echo "  3. Create view-model types in orc/presenters/include/*_view_models.h"
    echo "  4. Move business logic from GUI into presenter layer"
    echo ""
    echo "See architectural guidelines in docs/ for more information"
    echo ""
    exit 1
fi
