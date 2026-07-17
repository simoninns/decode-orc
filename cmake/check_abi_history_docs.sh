#!/usr/bin/env bash
# File:        check_abi_history_docs.sh
# Purpose:     Fail if the generated ABI version-history table in
#              docs/technical/plugin-sdk.md (between the GENERATED ABI VERSION
#              HISTORY markers) diverges from tools/gen_abi_history_docs.sh
#              output, i.e. the docs table is stale relative to
#              orc/sdk/abi_history.yaml. Regenerate with:
#                tools/gen_abi_history_docs.sh . (splice between the markers).
#
# Usage:       check_abi_history_docs.sh [<repo-root>]
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -u
REPO_ROOT="${1:-.}"
DOC="${REPO_ROOT}/docs/technical/plugin-sdk.md"
GEN="${REPO_ROOT}/tools/gen_abi_history_docs.sh"

tmp_gen="$(mktemp)"
tmp_doc="$(mktemp)"
trap 'rm -f "$tmp_gen" "$tmp_doc"' EXIT

bash "$GEN" "$REPO_ROOT" > "$tmp_gen"

# Extract the committed block, markers inclusive.
awk '
  /BEGIN GENERATED ABI VERSION HISTORY/ { inb=1 }
  inb { print }
  /END GENERATED ABI VERSION HISTORY/ { inb=0 }
' "$DOC" > "$tmp_doc"

if diff -u "$tmp_doc" "$tmp_gen" > /dev/null; then
    echo "✓ plugin-sdk.md ABI version-history table is in sync with abi_history.yaml"
    exit 0
fi
echo "❌ plugin-sdk.md ABI version-history table is stale relative to abi_history.yaml."
echo "   Regenerate the block between the GENERATED ABI VERSION HISTORY markers:"
echo "     tools/gen_abi_history_docs.sh . (splice its output into the doc)"
echo "   (< committed, > generated):"
diff "$tmp_doc" "$tmp_gen" | sed 's/^/   /' | head -40
exit 1
