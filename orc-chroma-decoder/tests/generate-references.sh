#!/bin/bash
#
# Generate baseline signatures for the chroma decoder test suite
# Run this script after manually verifying that the decoder output is correct
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIGNATURES_FILE="$SCRIPT_DIR/references/test-signatures.txt"

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "=========================================="
echo "Generate Baseline Signatures"
echo "=========================================="
echo ""

if [[ -f "$SIGNATURES_FILE" ]]; then
    echo -e "${YELLOW}WARNING:${NC} This will replace existing baseline signatures!"
    echo "Existing signatures: $SIGNATURES_FILE"
    echo ""
    read -p "Are you sure you want to continue? (yes/no): " confirm
    
    if [[ "$confirm" != "yes" ]]; then
        echo "Aborted."
        exit 0
    fi
    
    # Backup existing signatures
    BACKUP="${SIGNATURES_FILE}.backup-$(date +%Y%m%d-%H%M%S)"
    cp "$SIGNATURES_FILE" "$BACKUP"
    echo -e "${BLUE}[INFO]${NC} Backed up existing signatures to: $BACKUP"
    echo ""
fi

echo "Running tests in generate mode..."
echo ""

# Run the test suite in generate mode
"$SCRIPT_DIR/run-tests.sh" generate "$@"

EXIT_CODE=$?

if [[ $EXIT_CODE -eq 0 ]]; then
    echo ""
    echo -e "${GREEN}Baseline signatures have been generated!${NC}"
    echo ""
    echo "Signatures file: $SIGNATURES_FILE"
    echo "Total signatures: $(wc -l < "$SIGNATURES_FILE")"
    echo ""
    echo "Next steps:"
    echo "1. Review the signatures file"
    echo "2. Commit the signatures to version control:"
    echo "   git add $SIGNATURES_FILE"
    echo "   git commit -m 'Add baseline signatures for chroma decoder tests'"
    echo "3. Run verification tests:"
    echo "   ./run-tests.sh verify"
    echo ""
else
    echo ""
    echo "Failed to generate signatures (exit code: $EXIT_CODE)"
    exit $EXIT_CODE
fi
