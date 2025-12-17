#!/bin/bash
# Process all test-data files through orc-process
# Output goes to test-output/ directory (git-ignored)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORC_PROCESS="${SCRIPT_DIR}/build/bin/orc-process"
DAG_FILE="${SCRIPT_DIR}/examples/vbi-observers.yaml"
TEST_DATA="${SCRIPT_DIR}/test-data"
TEST_OUTPUT="${SCRIPT_DIR}/test-output"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Processing Test Data Files${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if orc-process exists
if [ ! -f "$ORC_PROCESS" ]; then
    echo -e "${YELLOW}Error: orc-process not found at $ORC_PROCESS${NC}"
    echo "Please build the project first:"
    echo "  cd orc/build && cmake .. && make"
    exit 1
fi

# Check if DAG file exists
if [ ! -f "$DAG_FILE" ]; then
    echo -e "${YELLOW}Error: DAG file not found at $DAG_FILE${NC}"
    exit 1
fi

# Create output directory structure
mkdir -p "$TEST_OUTPUT"

# Function to process a single TBC file
process_tbc() {
    local input_tbc="$1"
    local rel_path="${input_tbc#$TEST_DATA/}"  # Remove TEST_DATA prefix
    local dir_path="$(dirname "$rel_path")"
    local base_name="$(basename "$input_tbc" .tbc)"
    
    # Create output directory
    local output_dir="$TEST_OUTPUT/$dir_path"
    mkdir -p "$output_dir"
    
    # Output file
    local output_tbc="$output_dir/${base_name}_processed.tbc"
    
    echo -e "${GREEN}Processing:${NC} $rel_path"
    
    # Run orc-process
    if "$ORC_PROCESS" --dag "$DAG_FILE" "$input_tbc" "$output_tbc" > /dev/null 2>&1; then
        echo -e "  ${GREEN}✓${NC} Output: test-output/$dir_path/${base_name}_processed.tbc"
    else
        echo -e "  ${YELLOW}✗ Failed${NC}"
        return 1
    fi
    echo ""
}

# Find and process all TBC files
total_files=0
processed_files=0
failed_files=0

while IFS= read -r tbc_file; do
    ((total_files++))
    if process_tbc "$tbc_file"; then
        ((processed_files++))
    else
        ((failed_files++))
    fi
done < <(find "$TEST_DATA" -name "*.tbc" -type f | sort)

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Summary:${NC}"
echo -e "  Total files: $total_files"
echo -e "  ${GREEN}Processed: $processed_files${NC}"
if [ $failed_files -gt 0 ]; then
    echo -e "  ${YELLOW}Failed: $failed_files${NC}"
fi
echo ""
echo -e "Output directory: ${GREEN}$TEST_OUTPUT${NC}"
echo -e "View results with: ${BLUE}ld-analyse test-output/.../${NC}"
echo -e "${BLUE}========================================${NC}"
