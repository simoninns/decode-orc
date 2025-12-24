#!/bin/bash
#
# Performance profiling for ORC chroma decoder
# Tests decoder performance with 100-frame batches across different modes
# Separate from regression tests - focused on timing and optimization
#

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TEST_DATA_ROOT="$PROJECT_ROOT/test-data/laserdisc"
ORC_CLI="${ORC_CLI:-$PROJECT_ROOT/build/bin/orc-cli}"
OUTPUT_DIR="$PROJECT_ROOT/test-output/profile"
TEMP_DIR="$SCRIPT_DIR/profile-temp"
PROFILE_LOG="$SCRIPT_DIR/profile-results.txt"

# Frame count for profiling (100 frames = ~4 seconds of video)
FRAME_COUNT=100

# Usage information
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Profile ORC chroma decoder performance with 100-frame batches

OPTIONS:
    -h, --help              Show this help message
    --frames COUNT          Number of frames to process (default: 100)
    --quick                 Run only fastest decoders
    --full                  Run all decoder modes
    --compare               Compare all modes side-by-side

EXAMPLES:
    # Profile all decoder modes
    $0 --full

    # Quick profile of fastest modes
    $0 --quick

    # Compare all modes
    $0 --compare

    # Profile with 200 frames
    $0 --frames 200

EOF
    exit 0
}

# Parse arguments
QUICK_MODE=0
FULL_MODE=1
COMPARE_MODE=0

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            ;;
        --frames)
            FRAME_COUNT="$2"
            shift 2
            ;;
        --quick)
            QUICK_MODE=1
            FULL_MODE=0
            shift
            ;;
        --full)
            FULL_MODE=1
            shift
            ;;
        --compare)
            COMPARE_MODE=1
            shift
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# Verify orc-cli exists
if [[ ! -x "$ORC_CLI" ]]; then
    echo -e "${RED}Error: orc-cli not found or not executable: $ORC_CLI${NC}"
    echo "Build the project first:"
    echo "  cd $PROJECT_ROOT/build && cmake .. && make"
    exit 1
fi

# Verify test data exists
PAL_TBC="$TEST_DATA_ROOT/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc"
NTSC_TBC="$TEST_DATA_ROOT/ntsc/bambi/8000-8200/bambi_ntsc_clv_8000-8200.tbc"

if [[ ! -f "$PAL_TBC" ]]; then
    echo -e "${RED}Error: PAL test data not found: $PAL_TBC${NC}"
    exit 1
fi

if [[ ! -f "$NTSC_TBC" ]]; then
    echo -e "${RED}Error: NTSC test data not found: $NTSC_TBC${NC}"
    exit 1
fi

# Create directories
mkdir -p "$OUTPUT_DIR"
mkdir -p "$TEMP_DIR"

# Initialize results file
echo "ORC Chroma Decoder Performance Profile" > "$PROFILE_LOG"
echo "=======================================" >> "$PROFILE_LOG"
echo "Date: $(date)" >> "$PROFILE_LOG"
echo "ORC CLI: $ORC_CLI" >> "$PROFILE_LOG"
echo "Frames per test: $FRAME_COUNT" >> "$PROFILE_LOG"
echo "" >> "$PROFILE_LOG"

# Test results array
declare -A test_times
declare -A test_fps

# Create project file
create_project_file() {
    local test_name="$1"
    local system="$2"
    local decoder="$3"
    local output_file="$4"
    
    local tbc_path
    local source_stage
    
    if [[ "$system" == "PAL" ]]; then
        tbc_path="$PAL_TBC"
        source_stage="LDPALSource"
    else
        tbc_path="$NTSC_TBC"
        source_stage="LDNTSCSource"
    fi
    
    local project_file="$TEMP_DIR/${test_name}.orcprj"
    
    cat > "$project_file" << EOF
# ORC Project File - Performance Profile
project:
  name: ${system} Chroma Profile - ${decoder}
  version: 1.0
dag:
  nodes:
    - id: source
      stage: ${source_stage}
      node_type: SOURCE
      display_name: LD ${system} Source
      x: -400
      y: -300
      parameters:
        tbc_path:
          type: string
          value: ${tbc_path}
    - id: chroma
      stage: chroma_sink
      node_type: SINK
      display_name: Chroma Sink
      x: -100
      y: -300
      parameters:
        output_path:
          type: string
          value: ${output_file}
        decoder_type:
          type: string
          value: ${decoder}
        output_format:
          type: string
          value: rgb
        start_frame:
          type: integer
          value: 1
        length:
          type: integer
          value: ${FRAME_COUNT}
  edges:
    - from: source
      to: chroma
EOF
    
    echo "$project_file"
}

# Run a profile test
profile_test() {
    local test_name="$1"
    local system="$2"
    local decoder="$3"
    
    local output_file="$OUTPUT_DIR/${test_name}.rgb"
    
    echo -e "${CYAN}Profiling: ${test_name}${NC}"
    
    # Create project file
    local project_file
    project_file=$(create_project_file "$test_name" "$system" "$decoder" "$output_file")
    
    # Time the decoder
    local start_time=$(date +%s.%N)
    
    "$ORC_CLI" process "$project_file" --log-level info > "$TEMP_DIR/${test_name}.log" 2>&1
    
    local end_time=$(date +%s.%N)
    local exit_code=$?
    
    if [[ $exit_code -ne 0 ]]; then
        echo -e "  ${RED}FAILED${NC} (exit code: $exit_code)"
        echo "=== Error output ==="
        cat "$TEMP_DIR/${test_name}.log"
        echo "===================="
        return 1
    fi
    
    # Calculate timing
    local elapsed=$(echo "$end_time - $start_time" | bc)
    local fps=$(echo "scale=2; $FRAME_COUNT / $elapsed" | bc)
    
    # Store results
    test_times["$test_name"]=$elapsed
    test_fps["$test_name"]=$fps
    
    # Display result
    printf "  ${GREEN}%.2f seconds${NC} (%.2f fps)\n" "$elapsed" "$fps"
    
    # Log result
    printf "%-40s %8.2f sec  %8.2f fps\n" "$test_name" "$elapsed" "$fps" >> "$PROFILE_LOG"
    
    # Clean up output (keep log for analysis)
    rm -f "$output_file"
}

echo ""
echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}ORC Chroma Decoder Performance Profile${NC}"
echo -e "${BLUE}================================${NC}"
echo ""
echo "Processing $FRAME_COUNT frames per test"
echo ""

# PAL Decoders
echo -e "${YELLOW}=== PAL Decoders ===${NC}" | tee -a "$PROFILE_LOG"
echo "" >> "$PROFILE_LOG"

if [[ $QUICK_MODE -eq 1 ]]; then
    # Quick mode - only fastest decoders
    profile_test "PAL_2D" "PAL" "pal2d"
    profile_test "PAL_Mono" "PAL" "mono"
else
    # Full mode - all decoder types
    profile_test "PAL_2D" "PAL" "pal2d"
    profile_test "PAL_Transform2D" "PAL" "transform2d"
    profile_test "PAL_Transform3D" "PAL" "transform3d"
    profile_test "PAL_Mono" "PAL" "mono"
fi

echo ""
echo -e "${YELLOW}=== NTSC Decoders ===${NC}" | tee -a "$PROFILE_LOG"
echo "" >> "$PROFILE_LOG"

if [[ $QUICK_MODE -eq 1 ]]; then
    # Quick mode - only fastest decoders
    profile_test "NTSC_1D" "NTSC" "ntsc1d"
    profile_test "NTSC_Mono" "NTSC" "mono"
else
    # Full mode - all decoder types
    profile_test "NTSC_1D" "NTSC" "ntsc1d"
    profile_test "NTSC_2D" "NTSC" "ntsc2d"
    profile_test "NTSC_3D" "NTSC" "ntsc3d"
    profile_test "NTSC_3D_NoAdapt" "NTSC" "ntsc3dnoadapt"
    profile_test "NTSC_Mono" "NTSC" "mono"
fi

# Comparison summary
if [[ $COMPARE_MODE -eq 1 || $FULL_MODE -eq 1 ]]; then
    echo ""
    echo -e "${BLUE}=== Performance Summary ===${NC}" | tee -a "$PROFILE_LOG"
    echo "" >> "$PROFILE_LOG"
    
    # Find fastest and slowest
    fastest_test=""
    fastest_fps=0
    slowest_test=""
    slowest_fps=999999
    
    for test in "${!test_fps[@]}"; do
        fps=${test_fps[$test]}
        if (( $(echo "$fps > $fastest_fps" | bc -l) )); then
            fastest_fps=$fps
            fastest_test=$test
        fi
        if (( $(echo "$fps < $slowest_fps" | bc -l) )); then
            slowest_fps=$fps
            slowest_test=$test
        fi
    done
    
    echo -e "Fastest: ${GREEN}$fastest_test${NC} (${fastest_fps} fps)" | tee -a "$PROFILE_LOG"
    echo -e "Slowest: ${RED}$slowest_test${NC} (${slowest_fps} fps)" | tee -a "$PROFILE_LOG"
    echo "" >> "$PROFILE_LOG"
    
    # Speedup comparison
    echo "Speedup vs slowest:" | tee -a "$PROFILE_LOG"
    for test in "${!test_fps[@]}"; do
        fps=${test_fps[$test]}
        speedup=$(echo "scale=2; $fps / $slowest_fps" | bc)
        printf "  %-40s %6.2fx faster\n" "$test" "$speedup" | tee -a "$PROFILE_LOG"
    done
fi

echo ""
echo -e "${GREEN}Profile complete!${NC}"
echo "Results saved to: $PROFILE_LOG"
echo "Logs saved to: $TEMP_DIR/"
echo ""
