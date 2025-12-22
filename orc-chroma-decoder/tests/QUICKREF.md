# Chroma Decoder Test Suite - Quick Reference

## Two-Mode System

### Mode 1: Generate Baseline
```bash
./run-tests.sh generate
```
- Runs all tests
- Captures SHA-256 checksums of outputs
- Saves to `references/test-signatures.txt`
- Use when: First time setup, or after intentional decoder changes

### Mode 2: Verify (Default)
```bash
./run-tests.sh verify
# or just
./run-tests.sh
```
- Runs all tests
- Compares outputs against baseline signatures
- Fails if any signature doesn't match
- Use when: Testing for regressions during refactoring

## Common Commands

```bash
# First time - generate baseline
make generate

# Regular testing - verify no regressions
make test

# Quick smoke test
make quick

# After intentional changes - update baseline
make generate

# Clean outputs
make clean
```

## Signature File

All test signatures are stored in a single file:
```
references/test-signatures.txt
```

Format:
```
TEST_NAME|SHA256_CHECKSUM
PAL_2D_RGB|abc123...
NTSC_2D_RGB|def456...
```

## Test Coverage

**PAL Tests:**
- PAL 2D decoder (RGB, YUV, Y4M)
- Transform 2D decoder
- Transform 3D decoder
- Chroma gain/phase adjustments
- Monochrome output
- Custom line ranges

**NTSC Tests:**
- NTSC 1D, 2D, 3D decoders
- 3D no-adapt variant
- Chroma/luma noise reduction
- Phase compensation
- Monochrome output

**Edge Cases:**
- Reversed field order
- Output padding
- CAV vs CLV sources

## Refactoring Workflow

```bash
# 1. Generate baseline before refactoring
cd orc-chroma-decoder/tests
./run-tests.sh generate
git add references/test-signatures.txt
git commit -m "Baseline signatures before refactoring"

# 2. Make your refactoring changes
# ... edit source code ...

# 3. Rebuild
cd ../build && make && cd ../tests

# 4. Verify no regressions
./run-tests.sh verify

# If tests pass: refactoring preserved behavior ✓
# If tests fail: either fix bug or regenerate if intentional
```

## File Structure

```
tests/
├── run-tests.sh              # Main test runner (generate/verify modes)
├── generate-references.sh    # Helper script for generate mode
├── Makefile                  # Convenience targets
├── README.md                 # Full documentation
├── QUICKREF.md              # This file
├── references/
│   └── test-signatures.txt   # Single file with all signatures (commit this!)
├── outputs/                  # Test outputs (gitignored)
└── temp/                     # Logs (gitignored)
```

## Integration with Git

```bash
# The signatures file should be tracked
git add references/test-signatures.txt

# Output directories are ignored
# See .gitignore
```

## CI/CD Integration

See `.github/workflows/chroma-decoder-tests.yml` for GitHub Actions example.

Key points:
- Checkout code with test data
- Build decoder
- Run `./run-tests.sh verify` (or `verify --quick`)
- Signatures file must be in repository
