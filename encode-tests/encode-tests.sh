#!/bin/sh
#
# Test runner script for encode-orc YAML projects for decode-orc testing
# Runs all encode-project YAML files and outputs TBC files to encode-output directory
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Directories
ENCODE_PROJECTS_DIR="$SCRIPT_DIR/encode-projects"
ENCODE_OUTPUT_DIR="$SCRIPT_DIR/encode-output"

# Require encode-orc to be available on PATH
ENCODE_ORC=$(command -v encode-orc || true)
if [ -z "$ENCODE_ORC" ]; then
    echo -e "${RED}Error: encode-orc executable not found on PATH.${NC}"
    echo -e "${YELLOW}Please install encode-orc and try again.${NC}"
    exit 1
fi

# Set up ENCODE_ORC_ASSETS environment variable
# First check if already set in environment
if [ -z "$ENCODE_ORC_ASSETS" ]; then
    # Try local fallback location for development environments
    LOCAL_ASSETS_DIR="$PROJECT_ROOT/external/encode-orc/testcard-images"
    if [ -d "$LOCAL_ASSETS_DIR" ]; then
        export ENCODE_ORC_ASSETS="$LOCAL_ASSETS_DIR"
    else
        # If not set and local fallback doesn't exist, encode-orc will use its default
        # (typically set by nix installation or system configuration)
        echo -e "${YELLOW}Note: ENCODE_ORC_ASSETS not set and no local fallback found.${NC}"
        echo -e "${YELLOW}encode-orc will use its configured asset location.${NC}"
    fi
fi

# Set up ENCODE_ORC_OUTPUT_ROOT for test output
export ENCODE_ORC_OUTPUT_ROOT="$ENCODE_OUTPUT_DIR"

# Create output directory if it doesn't exist
mkdir -p "$ENCODE_OUTPUT_DIR"

# Count tests
total_tests=0
passed_tests=0
failed_tests=0

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  decode-orc Encode Test Suite${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Find all YAML files in encode-projects directory
if [ ! -d "$ENCODE_PROJECTS_DIR" ]; then
    echo -e "${RED}Error: encode-projects directory not found at $ENCODE_PROJECTS_DIR${NC}"
    exit 1
fi

# Find all YAML files recursively
mapfile -d '' yaml_files < <(find "$ENCODE_PROJECTS_DIR" -type f -name "*.yaml" -print0 | sort -z)

if [ ${#yaml_files[@]} -eq 0 ]; then
    echo -e "${YELLOW}Warning: No YAML test files found in $ENCODE_PROJECTS_DIR${NC}"
    exit 0
fi

# Run each test
for yaml_file in "${yaml_files[@]}"; do
    if [ -f "$yaml_file" ]; then
        total_tests=$((total_tests + 1))
        filename=$(basename "$yaml_file")
        
        # Get relative path from encode-projects directory
        rel_path="${yaml_file#$ENCODE_PROJECTS_DIR/}"
        rel_dir=$(dirname "$rel_path")
        
        # Display path for nested files
        if [ "$rel_dir" != "." ]; then
            display_name="$rel_dir/$filename"
        else
            display_name="$filename"
        fi
        
        echo -n "Test $total_tests: $display_name ... "
        
        # Run encode-orc with YAML file from project root
        cd "$PROJECT_ROOT"
        if "$ENCODE_ORC" "$yaml_file" > /dev/null 2>&1; then
            echo -e "${GREEN}PASSED${NC}"
            passed_tests=$((passed_tests + 1))
        else
            echo -e "${RED}FAILED${NC}"
            failed_tests=$((failed_tests + 1))
            echo "  └─ Run '$ENCODE_ORC $yaml_file' for details"
        fi
    fi
done

# Summary
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Test Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo "Total tests:  $total_tests"
echo -e "Passed:       ${GREEN}$passed_tests${NC}"

if [ $failed_tests -gt 0 ]; then
    echo -e "Failed:       ${RED}$failed_tests${NC}"
else
    echo -e "Failed:       ${GREEN}$failed_tests${NC}"
fi
echo ""

if [ $failed_tests -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
