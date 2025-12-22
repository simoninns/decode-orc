#!/bin/bash
#
# ORC Chroma Decoder Integration Test Suite
# Tests chroma decoding through orc-core (LD Source → Chroma Sink)
# Compares output signatures with standalone orc-chroma-decoder tool
#

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TEST_DATA_ROOT="$PROJECT_ROOT/test-data/laserdisc"
ORC_CLI="${ORC_CLI:-$PROJECT_ROOT/build/bin/orc-cli}"
STANDALONE_DECODER="$PROJECT_ROOT/orc-chroma-decoder/build/bin/orc-chroma-decoder"
OUTPUT_DIR="$PROJECT_ROOT/test-output"
TEMP_DIR="$SCRIPT_DIR/temp"
REFERENCE_DIR="$SCRIPT_DIR/references"

# Test counters
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0

# Flags
VERBOSE=${VERBOSE:-0}
MODE=${MODE:-compare}  # 'generate', 'verify', or 'compare'

# Helper functions
log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_success() { echo -e "${GREEN}[PASS]${NC} $*"; }
log_error() { echo -e "${RED}[FAIL]${NC} $*"; }
log_verbose() { [[ $VERBOSE -eq 1 ]] && echo -e "${NC}[DEBUG]${NC} $*"; }

# Calculate checksum
calculate_checksum() {
    sha256sum "$1" | awk '{print $1}'
}

# Create ORC project file dynamically
create_project_file() {
    local system="$1"        # "PAL" or "NTSC"
    local decoder="$2"       # "pal2d", "ntsc2d", etc.
    local output_format="$3" # "rgb", "yuv", "y4m"
    local extra_params="$4"  # Additional parameters (JSON fragment)
    local output_file="$5"
    
    local source_type="${system^^}Source"  # PALSource or NTSCSource
    local tbc_path db_path
    
    if [[ "$system" == "PAL" ]]; then
        tbc_path="$TEST_DATA_ROOT/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc"
        db_path="$TEST_DATA_ROOT/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc.json.db"
    else
        tbc_path="$TEST_DATA_ROOT/ntsc/bambi/18100-18306/bambi_ntsc_clv_18100-18306.tbc"
        db_path="$TEST_DATA_ROOT/ntsc/bambi/18100-18306/bambi_ntsc_clv_18100-18306.tbc.json.db"
    fi
    
    local project_file="$TEMP_DIR/test-${system,,}-${decoder}.orcprj"
    
    cat > "$project_file" << EOF
{
  "version": "1.0",
  "name": "${system} Chroma Test - ${decoder}",
  "nodes": [
    {
      "id": "source",
      "type": "LD${source_type}",
      "parameters": {
        "tbc_path": "${tbc_path}",
        "db_path": "${db_path}"
      }
    },
    {
      "id": "chroma",
      "type": "chroma_sink",
      "parameters": {
        "output_path": "${output_file}",
        "decoder_type": "${decoder}",
        "output_format": "${output_format}",
        "start_frame": 1,
        "length": 10
        ${extra_params}
      }
    }
  ],
  "connections": [
    {
      "from": "source",
      "from_output": 0,
      "to": "chroma",
      "to_input": 0
    }
  ]
}
EOF
    
    echo "$project_file"
}

# Run test through orc-cli
run_orc_test() {
    local test_name="$1"
    local system="$2"
    local decoder="$3"
    local output_format="$4"
    local extra_params="$5"
    
    ((TESTS_TOTAL++))
    
    log_info "Running ORC test: $test_name"
    
    local output_file="$OUTPUT_DIR/orc-${test_name,,}.${output_format}"
    local project_file=$(create_project_file "$system" "$decoder" "$output_format" "$extra_params" "$output_file")
    
    log_verbose "  Project: $project_file"
    log_verbose "  Output: $output_file"
    
    # Run orc-cli with trigger on chroma sink
    if [[ $VERBOSE -eq 1 ]]; then
        "$ORC_CLI" run "$project_file" --trigger chroma
    else
        "$ORC_CLI" run "$project_file" --trigger chroma > "$TEMP_DIR/${test_name}.log" 2>&1
    fi
    
    if [[ $? -ne 0 ]]; then
        log_error "Test failed: $test_name"
        ((TESTS_FAILED++))
        return 1
    fi
    
    if [[ ! -f "$output_file" ]]; then
        log_error "Output not created: $output_file"
        ((TESTS_FAILED++))
        return 1
    fi
    
    local orc_checksum=$(calculate_checksum "$output_file")
    log_verbose "  ORC checksum: $orc_checksum"
    
    # Compare with standalone decoder if in compare mode
    if [[ "$MODE" == "compare" ]] && [[ -f "$STANDALONE_DECODER" ]]; then
        compare_with_standalone "$test_name" "$system" "$decoder" "$output_format" "$orc_checksum"
    else
        log_success "Test completed: $test_name"
        ((TESTS_PASSED++))
    fi
}

# Compare with standalone decoder output
compare_with_standalone() {
    local test_name="$1"
    local system="$2"
    local decoder="$3"
    local output_format="$4"
    local orc_checksum="$5"
    
    local standalone_output="$OUTPUT_DIR/standalone-${test_name,,}.${output_format}"
    local tbc_path
    
    if [[ "$system" == "PAL" ]]; then
        tbc_path="$TEST_DATA_ROOT/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc"
    else
        tbc_path="$TEST_DATA_ROOT/ntsc/bambi/18100-18306/bambi_ntsc_clv_18100-18306.tbc"
    fi
    
    # Run standalone decoder with same parameters
    log_verbose "  Running standalone decoder for comparison..."
    
    local decoder_flag=""
    [[ "$decoder" != "${system,,}2d" ]] && decoder_flag="-f $decoder"
    
    if [[ $VERBOSE -eq 1 ]]; then
        "$STANDALONE_DECODER" -s 1 -l 10 $decoder_flag -p "$output_format" "$tbc_path" "$standalone_output"
    else
        "$STANDALONE_DECODER" -s 1 -l 10 $decoder_flag -p "$output_format" "$tbc_path" "$standalone_output" > /dev/null 2>&1
    fi
    
    if [[ ! -f "$standalone_output" ]]; then
        log_error "Standalone decoder failed to create output"
        ((TESTS_FAILED++))
        return 1
    fi
    
    local standalone_checksum=$(calculate_checksum "$standalone_output")
    log_verbose "  Standalone checksum: $standalone_checksum"
    
    if [[ "$orc_checksum" == "$standalone_checksum" ]]; then
        log_success "Test passed (matches standalone): $test_name"
        ((TESTS_PASSED++))
        return 0
    else
        log_error "Output mismatch: $test_name"
        log_error "  ORC:        $orc_checksum"
        log_error "  Standalone: $standalone_checksum"
        ((TESTS_FAILED++))
        return 1
    fi
}

# Main test suite
main() {
    log_info "==========================================="
    log_info "ORC Chroma Decoder Integration Tests"
    log_info "==========================================="
    
    # Check prerequisites
    if [[ ! -f "$ORC_CLI" ]]; then
        log_error "orc-cli not found: $ORC_CLI"
        exit 1
    fi
    
    if [[ "$MODE" == "compare" ]] && [[ ! -f "$STANDALONE_DECODER" ]]; then
        log_error "Standalone decoder not found: $STANDALONE_DECODER"
        log_info "Build it first or use MODE=verify"
        exit 1
    fi
    
    mkdir -p "$OUTPUT_DIR" "$TEMP_DIR" "$REFERENCE_DIR"
    rm -f "$OUTPUT_DIR"/orc-*.rgb "$OUTPUT_DIR"/orc-*.yuv "$OUTPUT_DIR"/standalone-*
    
    # PAL Tests
    log_info ""
    log_info "Testing PAL Decoders..."
    log_info "-------------------------------------------"
    
    run_orc_test "PAL_2D_RGB" "PAL" "pal2d" "rgb" ""
    run_orc_test "PAL_Transform2D_RGB" "PAL" "transform2d" "rgb" ""
    run_orc_test "PAL_Transform3D_RGB" "PAL" "transform3d" "rgb" ""
    run_orc_test "PAL_2D_YUV" "PAL" "pal2d" "yuv" ""
    run_orc_test "PAL_2D_Y4M" "PAL" "pal2d" "y4m" ""
    
    # NTSC Tests
    log_info ""
    log_info "Testing NTSC Decoders..."
    log_info "-------------------------------------------"
    
    run_orc_test "NTSC_2D_RGB" "NTSC" "ntsc2d" "rgb" ""
    run_orc_test "NTSC_3D_RGB" "NTSC" "ntsc3d" "rgb" ""
    run_orc_test "NTSC_2D_YUV" "NTSC" "ntsc2d" "yuv" ""
    
    # Summary
    log_info ""
    log_info "==========================================="
    log_info "Test Results"
    log_info "==========================================="
    log_info "Total:  $TESTS_TOTAL"
    log_success "Passed: $TESTS_PASSED"
    log_error "Failed: $TESTS_FAILED"
    
    if [[ $TESTS_FAILED -gt 0 ]]; then
        exit 1
    fi
}

main "$@"
