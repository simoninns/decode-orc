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
cd "$SCRIPT_DIR/.."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TOTAL_VIOLATIONS=0
SKIP_COMPILER_TESTS=0
COMPILER="${CXX:-c++}"

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



# =============================================================================
# CHECK 1: Interface Leakage Detection
# =============================================================================

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
    echo -e "MVP check 1: Interface Leakage (Core types in presenter public APIs) - ${GREEN}Passed${NC}"
else
    echo -e "MVP check 1: Interface Leakage (Core types in presenter public APIs) - ${RED}Failed${NC} ($PRESENTER_VIOLATIONS violation(s))"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + PRESENTER_VIOLATIONS))
fi

# =============================================================================
# CHECK 2: GUI Layer Violations
# =============================================================================

GUI_VIOLATIONS=0
GUI_HEADERS="orc/gui/*.h"

for header in $GUI_HEADERS; do
    if [[ ! -f "$header" ]]; then
        continue
    fi
    
    for core_type in "${FORBIDDEN_CORE_TYPES[@]}"; do
        # In GUI, even forward declarations are suspicious
        # Filter out comment-only lines (grep -n output format is "linenum:content")
        matches=$(grep -n "$core_type" "$header" | grep -v "^[0-9]*:[[:space:]]*/\|^[0-9]*:[[:space:]]*\*") || true
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
    echo -e "MVP check 2: GUI Layer (Core type references in GUI code) - ${GREEN}Passed${NC}"
else
    echo -e "MVP check 2: GUI Layer (Core type references in GUI code) - ${YELLOW}Warning${NC} ($GUI_VIOLATIONS reference(s))"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + GUI_VIOLATIONS))
fi

# =============================================================================
# CHECK 3: Compiler Enforcement Tests
# =============================================================================
if [[ $SKIP_COMPILER_TESTS -eq 0 ]]; then
    if ! command -v "$COMPILER" &> /dev/null; then
        echo -e "MVP check 3: Compiler Enforcement (Compile guards prevent direct includes) - Skipped (no C++ compiler found: $COMPILER)"
        SKIP_COMPILER_TESTS=1
    fi
fi

if [[ $SKIP_COMPILER_TESTS -eq 0 ]]; then
    
    COMPILER_FAILURES=0
    
    # Test 3.1: Core header inclusion should fail with ORC_GUI_BUILD
    cat > /tmp/test_mvp_violation.cpp << 'EOF'
#include "project.h"
void test_function() {}
EOF
    
    if ! "$COMPILER" -c /tmp/test_mvp_violation.cpp \
        -I orc/core/include \
        -I orc/common/include \
        -DORC_GUI_BUILD \
        2>&1 | grep -q "error.*GUI code cannot include"; then
        COMPILER_FAILURES=$((COMPILER_FAILURES + 1))
    fi
    
    # Test 3.2: Preview renderer header should also fail
    cat > /tmp/test_mvp_violation2.cpp << 'EOF'
#include "preview_renderer.h"
void test_function() {}
EOF
    
    if ! "$COMPILER" -c /tmp/test_mvp_violation2.cpp \
        -I orc/core/include \
        -I orc/common/include \
        -I orc/view-types \
        -DORC_GUI_BUILD \
        2>&1 | grep -q "error.*GUI code cannot include"; then
        COMPILER_FAILURES=$((COMPILER_FAILURES + 1))
    fi
    
    # Test 3.3: Public API should compile successfully
    cat > /tmp/test_mvp_valid.cpp << 'EOF'
#include <orc_rendering.h>
#include <parameter_types.h>
void test_function() {
    orc::PreviewImage img;
    orc::ParameterValue param = std::string("test");
}
EOF
    
    # Get spdlog include paths (for fmt dependency in node_id.h)
    SPDLOG_INCLUDES=""
    if command -v pkg-config &> /dev/null; then
        if pkg-config --exists spdlog 2>/dev/null; then
            SPDLOG_INCLUDES=$(pkg-config --cflags spdlog)
        fi
    fi
    
    if ! "$COMPILER" -c /tmp/test_mvp_valid.cpp \
        -I orc/view-types \
        -I orc/common/include \
        $SPDLOG_INCLUDES \
        -DORC_GUI_BUILD \
        -std=c++17 \
        2>&1 > /tmp/compile_output.txt; then
        cat /tmp/compile_output.txt
        COMPILER_FAILURES=$((COMPILER_FAILURES + 1))
    fi
    
    # Cleanup
    rm -f /tmp/test_mvp_*.cpp /tmp/compile_output.txt
    
    if [[ $COMPILER_FAILURES -eq 0 ]]; then
        echo -e "MVP check 3: Compiler Enforcement (Compile guards prevent direct includes) - ${GREEN}Passed${NC}"
    else
        echo -e "MVP check 3: Compiler Enforcement (Compile guards prevent direct includes) - ${RED}Failed${NC} ($COMPILER_FAILURES test(s) failed)"
        TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + COMPILER_FAILURES))
    fi
else
    echo -e "MVP check 3: Compiler Enforcement (Compile guards prevent direct includes) - Skipped"
fi

# =============================================================================
# Summary
# =============================================================================

if [[ $TOTAL_VIOLATIONS -eq 0 ]]; then
    exit 0
else
    echo ""
    echo -e "${RED}MVP architecture violations detected${NC}: $TOTAL_VIOLATIONS total violation(s)"
    exit 1
fi
