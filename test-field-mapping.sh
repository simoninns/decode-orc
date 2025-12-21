#!/bin/bash
#
# Field Mapping Test Automation Script
# 
# This script:
# 1. Runs ld-discmap on all corrupted test files
# 2. Creates ORC project files for each test file
# 3. Runs ORC field mapping analysis on each project
# 4. Compares results between ld-discmap and ORC
#
# Usage: ./test-field-mapping.sh [--generate-only] [--analyze-only]
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DATA_DIR="$SCRIPT_DIR/test-data/laserdisc/corrupted"
RESULTS_DIR="$SCRIPT_DIR/test-results"
PROJECTS_DIR="$SCRIPT_DIR/test-projects"
ORC_CLI="$SCRIPT_DIR/build/bin/orc-cli"

# Ensure build exists
if [ ! -f "$ORC_CLI" ]; then
    echo "ERROR: orc-cli not found at $ORC_CLI"
    echo "Please build the project first: cd build && make"
    exit 1
fi

# Ensure ld-discmap is available
if ! command -v ld-discmap &> /dev/null; then
    echo "ERROR: ld-discmap not found in PATH"
    echo "Please install or add to PATH"
    exit 1
fi

# Create output directories
mkdir -p "$RESULTS_DIR/ld-discmap"
mkdir -p "$RESULTS_DIR/orc"
mkdir -p "$PROJECTS_DIR"

# Parse command line options
GENERATE_ONLY=0
ANALYZE_ONLY=0
if [ "$1" = "--generate-only" ]; then
    GENERATE_ONLY=1
elif [ "$1" = "--analyze-only" ]; then
    ANALYZE_ONLY=1
fi

# Function to extract video format from filename
get_video_format() {
    local filename="$1"
    if [[ "$filename" =~ "_pal_" ]]; then
        echo "PAL"
    elif [[ "$filename" =~ "_ntsc_" ]]; then
        echo "NTSC"
    else
        echo "UNKNOWN"
    fi
}

# Function to get base name without extension
get_base_name() {
    local filepath="$1"
    basename "$filepath" .tbc
}

# Function to create ORC project file
create_orc_project() {
    local tbc_file="$1"
    local project_file="$2"
    local video_format="$3"
    
    # Get absolute path to TBC file
    local abs_tbc_path="$(cd "$(dirname "$tbc_file")" && pwd)/$(basename "$tbc_file")"
    
    # Determine source stage based on video format
    local source_stage=""
    local display_name=""
    if [ "$video_format" = "PAL" ]; then
        source_stage="LDPALSource"
        display_name="LD PAL Source"
    elif [ "$video_format" = "NTSC" ]; then
        source_stage="LDNTSCSource"
        display_name="LD NTSC Source"
    else
        echo "ERROR: Unknown video format: $video_format"
        return 1
    fi
    
    # Create project YAML
    cat > "$project_file" <<EOF
# ORC Project File
# Version: 1.0
# Auto-generated for testing

project:
  name: $(basename "$project_file" .orcprj)
  version: 1.0
dag:
  nodes:
    - id: node_1
      stage: $source_stage
      node_type: SOURCE
      display_name: $display_name
      user_label: $display_name
      x: -400
      y: -300
      parameters:
        tbc_path:
          type: string
          value: $abs_tbc_path
    - id: node_2
      stage: field_map
      node_type: TRANSFORM
      display_name: Field Map
      user_label: Field Map
      x: -100
      y: -300
      parameters:
        ranges:
          type: string
          value: ""
  edges:
    - from: node_1
      to: node_2
EOF
}

# Function to run ld-discmap and capture output
run_ld_discmap() {
    local tbc_file="$1"
    local output_file="$2"
    
    echo "  Running ld-discmap..."
    ld-discmap --debug --maponly "$tbc_file" > "$output_file" 2>&1 || true
}

# Function to run ORC analysis
run_orc_analysis() {
    local project_file="$1"
    local output_file="$2"
    
    echo "  Running ORC analysis..."
    "$ORC_CLI" analyze-field-mapping "$project_file" > "$output_file" 2>&1 || true
}

# Function to extract key stats from ld-discmap output
extract_ld_discmap_stats() {
    local output_file="$1"
    
    # Check if ld-discmap could process the file
    if grep -q "Could not process TBC metadata successfully" "$output_file"; then
        echo "UNMAPPABLE"
        return
    fi
    
    # Extract statistics from ld-discmap output
    # Note: ld-discmap uses "Info:" prefix for main stats
    local vbi_corrections=$(grep "Sequence analysis corrected" "$output_file" | grep -oP 'corrected \K\d+' || echo "0")
    local invalid_phase=$(grep "frames marked as invalid due to incorrect phase" "$output_file" | grep -oP 'Info: Removing \K\d+' || echo "0")
    local duplicates=$(grep "Removed.*duplicate frames - disc map size" "$output_file" | grep -oP 'Removed \K\d+' || echo "0")
    local gaps=$(grep "Found.*gaps representing" "$output_file" | grep -oP 'Found \K\d+' || echo "0")
    local padding=$(grep "gaps representing.*missing frames" "$output_file" | grep -oP 'representing \K\d+' || echo "0")
    
    echo "$vbi_corrections,$invalid_phase,$duplicates,$gaps,$padding"
}

# Function to extract key stats from ORC output
extract_orc_stats() {
    local output_file="$1"
    
    # Extract statistics from ORC output
    local vbi_corrections=$(grep -oP 'correctedVBIErrors: \K\d+' "$output_file" || echo "0")
    local invalid_phase=$(grep -oP 'removedInvalidPhase: \K\d+' "$output_file" || echo "0")
    local duplicates=$(grep -oP 'removedDuplicates: \K\d+' "$output_file" || echo "0")
    local gaps=$(grep -oP 'gapsPadded: \K\d+' "$output_file" || echo "0")
    local padding=$(grep -oP 'paddingFrames: \K\d+' "$output_file" || echo "0")
    
    echo "$vbi_corrections,$invalid_phase,$duplicates,$gaps,$padding"
}

# Main processing loop
echo "========================================="
echo "Field Mapping Test Automation"
echo "========================================="
echo ""

# Initialize results CSV
RESULTS_CSV="$RESULTS_DIR/comparison.csv"
if [ $ANALYZE_ONLY -eq 0 ]; then
    echo "TestFile,Format,LD_VBICorrections,LD_InvalidPhase,LD_Duplicates,LD_Gaps,LD_Padding,ORC_VBICorrections,ORC_InvalidPhase,ORC_Duplicates,ORC_Gaps,ORC_Padding,Match" > "$RESULTS_CSV"
fi

# Find all TBC files in corrupted directory
test_count=0
pass_count=0
fail_count=0

while IFS= read -r tbc_file; do
    test_count=$((test_count + 1))
    
    base_name=$(get_base_name "$tbc_file")
    video_format=$(get_video_format "$base_name")
    
    echo "[$test_count/49] Processing: $base_name"
    echo "  Format: $video_format"
    
    # Define output paths
    ld_output="$RESULTS_DIR/ld-discmap/${base_name}.txt"
    orc_output="$RESULTS_DIR/orc/${base_name}.txt"
    project_file="$PROJECTS_DIR/${base_name}.orcprj"
    
    # Step 1: Run ld-discmap (unless analyze-only mode)
    if [ $ANALYZE_ONLY -eq 0 ]; then
        run_ld_discmap "$tbc_file" "$ld_output"
    fi
    
    # Step 2: Create ORC project (unless analyze-only mode)
    if [ $ANALYZE_ONLY -eq 0 ]; then
        create_orc_project "$tbc_file" "$project_file" "$video_format"
    fi
    
    # Step 3: Run ORC analysis (unless generate-only mode)
    if [ $GENERATE_ONLY -eq 0 ]; then
        run_orc_analysis "$project_file" "$orc_output"
        
        # Extract and compare statistics
        ld_stats=$(extract_ld_discmap_stats "$ld_output")
        orc_stats=$(extract_orc_stats "$orc_output")
        
        # Check if ld-discmap could process the file
        if [ "$ld_stats" = "UNMAPPABLE" ]; then
            match="SKIP"
            echo "  Result: - SKIP (no VBI frame numbers)"
            echo "${base_name},${video_format},N/A,N/A,N/A,N/A,N/A,${orc_stats},${match}" >> "$RESULTS_CSV"
        else
            # Check if stats match
            if [ "$ld_stats" = "$orc_stats" ]; then
                match="PASS"
                pass_count=$((pass_count + 1))
                echo "  Result: ✓ PASS"
            else
                match="FAIL"
                fail_count=$((fail_count + 1))
                echo "  Result: ✗ FAIL"
                echo "    ld-discmap: $ld_stats"
                echo "    ORC:        $orc_stats"
            fi
            
            # Append to results CSV
            echo "${base_name},${video_format},${ld_stats},${orc_stats},${match}" >> "$RESULTS_CSV"
        fi
    else
        echo "  Project created: $project_file"
    fi
    
    echo ""
done < <(find "$TEST_DATA_DIR" -name "*.tbc" -type f | sort)

# Summary
echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Total tests: $test_count"

if [ $GENERATE_ONLY -eq 0 ]; then
    echo "Passed: $pass_count"
    echo "Failed: $fail_count"
    echo ""
    echo "Detailed results: $RESULTS_CSV"
    
    if [ $fail_count -gt 0 ]; then
        echo ""
        echo "Failed tests:"
        grep "FAIL" "$RESULTS_CSV" | cut -d',' -f1
    fi
else
    echo ""
    echo "Projects generated in: $PROJECTS_DIR"
    echo "Run with --analyze-only to test without regenerating projects"
fi

echo ""
echo "Results directory: $RESULTS_DIR"
echo "Projects directory: $PROJECTS_DIR"
