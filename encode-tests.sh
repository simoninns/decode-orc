#!/bin/bash
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
PROJECT_ROOT="$SCRIPT_DIR"

# Directories
ENCODE_PROJECTS_DIR="$PROJECT_ROOT/encode-projects"
ENCODE_OUTPUT_DIR="$PROJECT_ROOT/encode-output"
BUILD_DIR="$PROJECT_ROOT/build/external/encode-orc"

# Check if decode-orc has been built (which builds encode-orc)
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: encode-orc build not found at $BUILD_DIR${NC}"
    echo -e "${YELLOW}Please build decode-orc first (which builds encode-orc as a dependency)${NC}"
    exit 1
fi

ENCODE_ORC="$BUILD_DIR/encode-orc"

if [ ! -f "$ENCODE_ORC" ]; then
    echo -e "${RED}Error: encode-orc executable not found. Build may have failed.${NC}"
    exit 1
fi

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

yaml_files=("$ENCODE_PROJECTS_DIR"/*.yaml)

if [ ${#yaml_files[@]} -eq 0 ] || [ ! -e "${yaml_files[0]}" ]; then
    echo -e "${YELLOW}Warning: No YAML test files found in $ENCODE_PROJECTS_DIR${NC}"
    exit 0
fi

# Run each test
for yaml_file in "${yaml_files[@]}"; do
    if [ -f "$yaml_file" ]; then
        total_tests=$((total_tests + 1))
        filename=$(basename "$yaml_file")
        
        echo -n "Test $total_tests: $filename ... "
        
        # Run encode-orc with YAML file from project root
        cd "$PROJECT_ROOT"
        if "$ENCODE_ORC" "$yaml_file" > /dev/null 2>&1; then
            echo -e "${GREEN}PASSED${NC}"
            passed_tests=$((passed_tests + 1))
            
            # Extract output filename from YAML to verify it was created
            output_file=$(grep "filename:" "$yaml_file" | head -1 | sed 's/.*filename:[[:space:]]*"\([^"]*\)".*/\1/')
            if [ -n "$output_file" ]; then
                # Check for both .tbc and .tbc.json files
                tbc_file="$PROJECT_ROOT/${output_file}.tbc"
                json_file="$PROJECT_ROOT/${output_file}.tbc.json"
                
                if [ -f "$tbc_file" ]; then
                    file_size=$(stat -c%s "$tbc_file" 2>/dev/null || stat -f%z "$tbc_file" 2>/dev/null)
                    size_human=$(numfmt --to=iec-i --suffix=B "$file_size" 2>/dev/null || echo "$file_size bytes")
                    echo "  ├─ TBC: ${output_file}.tbc ($size_human)"
                fi
                
                if [ -f "$json_file" ]; then
                    echo "  └─ Metadata: ${output_file}.tbc.json"
                fi
            fi
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
