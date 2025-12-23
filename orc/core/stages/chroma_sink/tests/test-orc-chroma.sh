#!/bin/bash
#
# ORC Chroma Decoder Integration Test Suite
# Tests chroma decoding through orc-core (LD Source → Chroma Sink)
# Compares output signatures with standalone orc-chroma-decoder tool
#
# Usage:
#   ./test-orc-chroma.sh [verify|compare|generate]
#
# Modes:
#   verify   - Compare ORC output to stored reference signatures (default, fast)
#   compare  - Compare ORC output to freshly-run standalone decoder (slow)
#   generate - Generate new reference signatures (only when decoder changes)
#
# Environment variables:
#   VERBOSE=1      - Show detailed progress information
#   DEBUG=1        - Show full debug output including project files and orc-cli execution
#   SINGLE_TEST=N  - Run only test number N (1-9, see test list below)
#   MODE           - Test mode (verify, compare, generate) - can also be first argument
#
# Examples:
#   ./test-orc-chroma.sh                   # Run all tests against reference signatures
#   ./test-orc-chroma.sh verify            # Same as above
#   ./test-orc-chroma.sh compare           # Compare with standalone decoder
#   VERBOSE=1 ./test-orc-chroma.sh         # Run with verbose output
#   DEBUG=1 ./test-orc-chroma.sh           # Run with full debug output
#   SINGLE_TEST=1 ./test-orc-chroma.sh     # Run only test #1 (PAL_2D_RGB)
#   SINGLE_TEST=5 VERBOSE=1 ./test-orc-chroma.sh  # Run test #5 with verbose output
#
# Test List:
#   1. PAL_2D_RGB
#   2. PAL_Transform2D_RGB
#   3. PAL_Transform3D_RGB
#   4. PAL_2D_YUV
#   5. PAL_2D_Y4M
#   6. NTSC_1D_RGB
#   7. NTSC_2D_RGB
#   8. NTSC_3D_RGB
#   9. NTSC_2D_YUV
#
# Test Strategy:
#   This script runs the same 24 tests as the standalone orc-chroma-decoder test suite.
#   Each test exercises different decoder types, output formats, and parameters.
#   In 'verify' mode (default), output signatures are compared to pre-generated references.
#   In 'compare' mode, the standalone decoder is run and outputs are compared directly.
#

# Removed set -e to prevent silent failures - we handle errors explicitly

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
REFERENCE_SIGS="$PROJECT_ROOT/orc-chroma-decoder/tests/references/test-signatures.txt"
OUTPUT_DIR="$PROJECT_ROOT/test-output"
TEMP_DIR="$SCRIPT_DIR/temp"
REFERENCE_DIR="$SCRIPT_DIR/references"

# Test counters
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0

# Flags
VERBOSE=${VERBOSE:-0}
DEBUG=${DEBUG:-0}
SINGLE_TEST=${SINGLE_TEST:-0}  # Set to test number (1-9) to run only that test
MODE=${MODE:-verify}  # 'verify' (compare to reference), 'compare' (compare to standalone), 'generate' (generate new references)

# Helper functions
log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_success() { echo -e "${GREEN}[PASS]${NC} $*"; }
log_error() { echo -e "${RED}[FAIL]${NC} $*"; }
log_verbose() { [[ $VERBOSE -eq 1 || $DEBUG -eq 1 ]] && echo -e "${NC}[DEBUG]${NC} $*"; }
log_debug() { [[ $DEBUG -eq 1 ]] && echo -e "${YELLOW}[DEBUG]${NC} $*"; }

# Calculate checksum
calculate_checksum() {
    sha256sum "$1" | awk '{print $1}'
}

# Create ORC project file dynamically
create_project_file() {
    local system="$1"        # "PAL" or "NTSC"
    local decoder="$2"       # "pal2d", "ntsc2d", etc.
    local output_format="$3" # "rgb", "yuv", "y4m"
    local extra_params="$4"  # Additional parameters (YAML fragment)
    local output_file="$5"
    
    local source_stage="LD${system}Source"
    local tbc_path
    
    if [[ "$system" == "PAL" ]]; then
        tbc_path="$TEST_DATA_ROOT/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc"
    else
        tbc_path="$TEST_DATA_ROOT/ntsc/bambi/18100-18306/bambi_ntsc_clv_18100-18306.tbc"
    fi
    
    local system_lower=$(echo "$system" | tr '[:upper:]' '[:lower:]')
    local project_file="$TEMP_DIR/test-${system_lower}-${decoder}.orcprj"
    
    cat > "$project_file" << EOF
# ORC Project File - Auto-generated test
# Version: 1.0

project:
  name: ${system} Chroma Test - ${decoder}
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
          value: ${output_format}
        start_frame:
          type: integer
          value: 1
        length:
          type: integer
          value: 10${extra_params}
  edges:
    - from: source
      to: chroma
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
    
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    
    # Skip if SINGLE_TEST is set and doesn't match current test number
    if [[ $SINGLE_TEST -gt 0 ]] && [[ $SINGLE_TEST -ne $TESTS_TOTAL ]]; then
        return 0
    fi
    
    log_info "Running ORC test #$TESTS_TOTAL: $test_name"
    
    local test_name_lower=$(echo "$test_name" | tr '[:upper:]' '[:lower:]')
    local output_file="$OUTPUT_DIR/orc-${test_name_lower}.${output_format}"
    local project_file
    project_file=$(create_project_file "$system" "$decoder" "$output_format" "$extra_params" "$output_file")
    
    if [[ -z "$project_file" || ! -f "$project_file" ]]; then
        log_error "Failed to create project file for test: $test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    log_verbose "  Project: $project_file"
    log_verbose "  Output: $output_file"
    
    if [[ $DEBUG -eq 1 ]]; then
        log_debug "Project file contents:"
        cat "$project_file"
    fi
    
    # Run orc-cli with process command (automatically triggers all sinks)
    log_info "  Executing orc-cli..."
    
    local exit_code=0
    "$ORC_CLI" process "$project_file" > "$TEMP_DIR/${test_name}.log" 2>&1 || exit_code=$?
    
    log_info "  Exit code: $exit_code"
    
    if [[ $exit_code -ne 0 ]]; then
        log_error "Test failed: $test_name (exit code: $exit_code)"
        log_error "=== Error output ==="
        cat "$TEMP_DIR/${test_name}.log"
        log_error "===================="
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    if [[ $exit_code -ne 0 ]]; then
        log_error "Test failed: $test_name (exit code: $exit_code)"
        log_error "=== Error output ==="
        cat "$TEMP_DIR/${test_name}.log"
        log_error "===================="
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    if [[ ! -f "$output_file" ]]; then
        log_error "Output not created: $output_file"
        log_error "=== orc-cli output ==="
        cat "$TEMP_DIR/${test_name}.log"
        log_error "======================"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    local orc_checksum=$(calculate_checksum "$output_file")
    log_verbose "  ORC checksum: $orc_checksum"
    
    # Compare based on mode
    if [[ "$MODE" == "verify" ]]; then
        verify_with_reference "$test_name" "$orc_checksum"
    elif [[ "$MODE" == "compare" ]] && [[ -f "$STANDALONE_DECODER" ]]; then
        compare_with_standalone "$test_name" "$system" "$decoder" "$output_format" "$orc_checksum"
    elif [[ "$MODE" == "generate" ]]; then
        echo "$test_name|$orc_checksum" >> "$TEMP_DIR/new-signatures.txt"
        log_success "Test completed: $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_success "Test completed: $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi
}

# Verify against reference signatures
verify_with_reference() {
    local test_name="$1"
    local orc_checksum="$2"
    
    if [[ ! -f "$REFERENCE_SIGS" ]]; then
        log_error "Reference signatures file not found: $REFERENCE_SIGS"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    local ref_checksum=$(grep "^${test_name}|" "$REFERENCE_SIGS" | cut -d'|' -f2)
    
    if [[ -z "$ref_checksum" ]]; then
        log_error "No reference signature found for: $test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    log_verbose "  Reference checksum: $ref_checksum"
    
    if [[ "$orc_checksum" == "$ref_checksum" ]]; then
        log_success "Test passed (matches reference): $test_name"
        log_info "  ORC:       $orc_checksum"
        log_info "  Reference: $ref_checksum"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        log_error "Output mismatch: $test_name"
        log_error "  ORC:       $orc_checksum"
        log_error "  Reference: $ref_checksum"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
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
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    local standalone_checksum=$(calculate_checksum "$standalone_output")
    log_verbose "  Standalone checksum: $standalone_checksum"
    
    if [[ "$orc_checksum" == "$standalone_checksum" ]]; then
        log_success "Test passed (matches standalone): $test_name"
        log_info "  ORC:        $orc_checksum"
        log_info "  Standalone: $standalone_checksum"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        log_error "Output mismatch: $test_name"
        log_error "  ORC:        $orc_checksum"
        log_error "  Standalone: $standalone_checksum"
        TESTS_FAILED=$((TESTS_FAILED + 1))
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
    
    # Check required test data files
    log_info "Checking test data availability..."
    
    local pal_tbc="$TEST_DATA_ROOT/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc"
    local pal_db="$TEST_DATA_ROOT/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc.db"
    local ntsc_tbc="$TEST_DATA_ROOT/ntsc/bambi/18100-18306/bambi_ntsc_clv_18100-18306.tbc"
    local ntsc_db="$TEST_DATA_ROOT/ntsc/bambi/18100-18306/bambi_ntsc_clv_18100-18306.tbc.db"
    
    if [[ ! -f "$pal_tbc" ]]; then
        log_error "PAL test data not found: $pal_tbc"
        log_info "Please ensure test data is available in $TEST_DATA_ROOT"
        exit 1
    fi
    
    if [[ ! -f "$pal_db" ]]; then
        log_error "PAL database not found: $pal_db"
        log_info "Please ensure test data is available in $TEST_DATA_ROOT"
        exit 1
    fi
    
    if [[ ! -f "$ntsc_tbc" ]]; then
        log_error "NTSC test data not found: $ntsc_tbc"
        log_info "Please ensure test data is available in $TEST_DATA_ROOT"
        exit 1
    fi
    
    if [[ ! -f "$ntsc_db" ]]; then
        log_error "NTSC database not found: $ntsc_db"
        log_info "Please ensure test data is available in $TEST_DATA_ROOT"
        exit 1
    fi
    
    log_success "All required test data files found"
    
    mkdir -p "$OUTPUT_DIR" "$TEMP_DIR" "$REFERENCE_DIR"
    rm -f "$OUTPUT_DIR"/orc-*.rgb "$OUTPUT_DIR"/orc-*.yuv "$OUTPUT_DIR"/orc-*.y4m "$OUTPUT_DIR"/standalone-*
    [[ "$MODE" == "generate" ]] && rm -f "$TEMP_DIR/new-signatures.txt"
    
    # PAL Basic Tests (should match standalone)
    log_info ""
    log_info "Testing PAL Decoders..."
    log_info "-------------------------------------------"
    
    run_orc_test "PAL_2D_RGB" "PAL" "pal2d" "rgb" ""
    run_orc_test "PAL_Transform2D_RGB" "PAL" "transform2d" "rgb" ""
    run_orc_test "PAL_Transform3D_RGB" "PAL" "transform3d" "rgb" ""
    run_orc_test "PAL_2D_YUV" "PAL" "pal2d" "yuv" ""
    run_orc_test "PAL_2D_Y4M" "PAL" "pal2d" "y4m" ""
    
    # NTSC Basic Tests (should match standalone)
    log_info ""
    log_info "Testing NTSC Decoders..."
    log_info "-------------------------------------------"
    
    run_orc_test "NTSC_1D_RGB" "NTSC" "ntsc1d" "rgb" ""
    run_orc_test "NTSC_2D_RGB" "NTSC" "ntsc2d" "rgb" ""
    run_orc_test "NTSC_3D_RGB" "NTSC" "ntsc3d" "rgb" ""
    run_orc_test "NTSC_2D_YUV" "NTSC" "ntsc2d" "yuv" ""
    
    # TODO: Add remaining tests once basic integration is working
    # - Mono decoder tests
    # - Chroma gain/phase tests
    # - Noise reduction tests
    # - CAV disc tests
    # - Reverse fields, padding, custom lines tests
    
    # Summary
    log_info ""
    log_info "==========================================="
    log_info "Test Results"
    log_info "==========================================="
    log_info "Mode:    $MODE"
    log_info "Total:   $TESTS_TOTAL"
    log_success "Passed:  $TESTS_PASSED"
    log_error "Failed: $TESTS_FAILED"
    
    if [[ $TESTS_FAILED -gt 0 ]]; then
        exit 1
    fi
}

main "$@"
