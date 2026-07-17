#!/usr/bin/env bash
# File:        gen_abi_history_docs.sh
# Purpose:     Emit the ABI/API "Version history" table (Markdown) from the
#              single source of truth orc/sdk/abi_history.yaml, to stdout. The
#              block is spliced into docs/technical/plugin-sdk.md between the
#              GENERATED ABI VERSION HISTORY markers; the AbiVersionDocsSync
#              CTest fails if the committed block and this output diverge.
#
# Usage:       tools/gen_abi_history_docs.sh [<repo-root>]
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu
REPO_ROOT="${1:-.}"
HISTORY="${REPO_ROOT}/orc/sdk/abi_history.yaml"

if [ ! -f "$HISTORY" ]; then
    echo "gen_abi_history_docs: history file not found: $HISTORY" >&2
    exit 1
fi

# Flatten the history list to Markdown rows. `summary` is a folded (>-) scalar
# spanning one or more 6-space-indented continuation lines and is always the
# last field of an entry, so everything from `summary:` up to the next `- abi:`
# item belongs to it (joined with single spaces, folded-scalar semantics).
emit_rows() {
    awk '
        function flush() {
            if (!has) return
            gsub(/^[ ]+|[ ]+$/, "", summ)
            printf "| %s | %s | %s |\n", abi, (api==""?"—":api), summ
        }
        /^[[:space:]]*-[[:space:]]*abi:/ {
            flush()
            has=1; api=""; summ=""; insumm=0
            v=$0; sub(/^[[:space:]]*-[[:space:]]*abi:[[:space:]]*/,"",v); abi=v
            next
        }
        /^    api:/     { v=$0; sub(/^    api:[[:space:]]*/,"",v); gsub(/"/,"",v); api=v; insumm=0; next }
        /^    cause:/   { insumm=0; next }
        /^    contracts:/ { insumm=0; next }
        /^      -[[:space:]]/ { if (!insumm) next }
        /^    summary:/ { insumm=1; next }
        {
            if (insumm) {
                line=$0
                gsub(/^[ ]+|[ ]+$/, "", line)
                if (line != "") summ = (summ==""? line : summ " " line)
            }
        }
        END { flush() }
    ' "$HISTORY"
}

echo '<!-- BEGIN GENERATED ABI VERSION HISTORY (source: orc/sdk/abi_history.yaml; regenerate with tools/gen_abi_history_docs.sh) -->'
echo
echo '| `host_abi_version` | `plugin_api_version` | Change |'
echo '|--------------------|----------------------|--------|'
emit_rows
echo
echo '<!-- END GENERATED ABI VERSION HISTORY -->'
