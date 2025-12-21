#!/bin/bash
# Verify Project encapsulation is maintained
# Run this to check that no code violates the encapsulation architecture

echo "==================================================================="
echo "Project Encapsulation Verification"
echo "==================================================================="
echo ""

ERRORS=0

echo "Checking for direct access to private Project fields..."
echo "--------------------------------------------------------"

# Check for direct field access patterns (should find NONE)
PATTERNS=(
    "project\\.name[^_]"
    "project\\.description[^_]"
    "project\\.version[^_]"
    "project\\.nodes[^_]"
    "project\\.edges[^_]"
    "project\\.is_modified[^_]"
)

DIRS=("orc/gui" "orc/cli")

for pattern in "${PATTERNS[@]}"; do
    echo -n "  Checking pattern: $pattern ... "
    matches=0
    for dir in "${DIRS[@]}"; do
        if [ -d "$dir" ]; then
            count=$(grep -r -E "$pattern" "$dir" 2>/dev/null | grep -v "Binary file" | wc -l)
            matches=$((matches + count))
        fi
    done
    
    if [ $matches -eq 0 ]; then
        echo "✓ OK"
    else
        echo "✗ FAILED ($matches violations found)"
        ERRORS=$((ERRORS + 1))
        grep -rn -E "$pattern" "${DIRS[@]}" 2>/dev/null | grep -v "Binary file"
    fi
done

echo ""
echo "Checking for proper use of getters..."
echo "--------------------------------------"

# These should be found - proper getter usage
GOOD_PATTERNS=(
    "get_name()"
    "get_description()"
    "get_version()"
    "get_nodes()"
    "get_edges()"
)

for pattern in "${GOOD_PATTERNS[@]}"; do
    echo -n "  Looking for: $pattern ... "
    matches=0
    for dir in "${DIRS[@]}"; do
        if [ -d "$dir" ]; then
            count=$(grep -r "$pattern" "$dir" 2>/dev/null | grep -v "Binary file" | wc -l)
            matches=$((matches + count))
        fi
    done
    
    if [ $matches -gt 0 ]; then
        echo "✓ Found ($matches uses)"
    else
        echo "⚠ Not used (might be OK)"
    fi
done

echo ""
echo "Checking for proper use of project_io functions..."
echo "---------------------------------------------------"

# These should be found - proper project_io usage
PROJECT_IO_PATTERNS=(
    "project_io::add_node"
    "project_io::remove_node"
    "project_io::set_node_parameters"
    "project_io::add_edge"
    "project_io::remove_edge"
)

for pattern in "${PROJECT_IO_PATTERNS[@]}"; do
    echo -n "  Looking for: $pattern ... "
    matches=0
    for dir in "${DIRS[@]}"; do
        if [ -d "$dir" ]; then
            count=$(grep -r "$pattern" "$dir" 2>/dev/null | grep -v "Binary file" | wc -l)
            matches=$((matches + count))
        fi
    done
    
    if [ $matches -gt 0 ]; then
        echo "✓ Found ($matches uses)"
    else
        echo "⚠ Not used (might be OK)"
    fi
done

echo ""
echo "==================================================================="
if [ $ERRORS -eq 0 ]; then
    echo "✓ VERIFICATION PASSED - Encapsulation is maintained"
    echo "==================================================================="
    exit 0
else
    echo "✗ VERIFICATION FAILED - $ERRORS violation(s) found"
    echo "==================================================================="
    echo ""
    echo "Fix these violations by:"
    echo "  1. Using project.get_X() for read access"
    echo "  2. Using project_io::function() for modifications"
    echo ""
    echo "See docs/PROJECT-ENCAPSULATION.md for details"
    exit 1
fi
