#!/usr/bin/env bash
# File:        check_abi_history.sh
# Purpose:     Guard that orc/sdk/abi_history.yaml is the single source of truth
#              for the ABI/API version log: its last entry's `abi`/`api` values
#              must equal the `kStagePluginHostAbiVersion` /
#              `kStagePluginApiVersion` constants in
#              orc/sdk/include/orc/abi/orc_plugin_abi.h. Bumping the header
#              constant without appending a matching abi_history.yaml entry
#              fails this gate (CTest label "sdk").
#
# Usage:       check_abi_history.sh [<repo-root>]
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -u
REPO_ROOT="${1:-.}"
HEADER="${REPO_ROOT}/orc/sdk/include/orc/abi/orc_plugin_abi.h"
HISTORY="${REPO_ROOT}/orc/sdk/abi_history.yaml"

fail=0

if [ ! -f "$HEADER" ];  then echo "❌ header not found: $HEADER";  exit 1; fi
if [ ! -f "$HISTORY" ]; then echo "❌ history not found: $HISTORY"; exit 1; fi

# Constants from the header.
header_abi="$(sed -n -E 's/.*kStagePluginHostAbiVersion[[:space:]]*=[[:space:]]*([0-9]+).*/\1/p' "$HEADER" | head -1)"
header_api="$(sed -n -E 's/.*kStagePluginApiVersion[[:space:]]*=[[:space:]]*([0-9]+).*/\1/p' "$HEADER" | head -1)"

# Last (highest) entry from the history file. Entries are ordered ascending, so
# the final `- abi:` / `api:` pair is the current one.
hist_abi="$(sed -n -E 's/^[[:space:]]*-[[:space:]]*abi:[[:space:]]*([0-9]+).*/\1/p' "$HISTORY" | tail -1)"
hist_api="$(sed -n -E 's/^[[:space:]]*api:[[:space:]]*"?([0-9]+)"?.*/\1/p' "$HISTORY" | tail -1)"

if [ -z "$header_abi" ]; then echo "❌ could not read kStagePluginHostAbiVersion from $HEADER"; fail=1; fi
if [ -z "$header_api" ]; then echo "❌ could not read kStagePluginApiVersion from $HEADER"; fail=1; fi

if [ "$header_abi" != "$hist_abi" ]; then
    echo "❌ ABI version mismatch: header kStagePluginHostAbiVersion=${header_abi}"
    echo "   but the last orc/sdk/abi_history.yaml entry is abi=${hist_abi}."
    echo "   Append a new entry (abi: ${header_abi}, api, cause, contracts,"
    echo "   summary) to abi_history.yaml, then regenerate the docs table:"
    echo "     tools/gen_abi_history_docs.sh . (splice into plugin-sdk.md)"
    fail=1
fi

if [ "$header_api" != "$hist_api" ]; then
    echo "❌ API version mismatch: header kStagePluginApiVersion=${header_api}"
    echo "   but the last orc/sdk/abi_history.yaml entry is api=${hist_api}."
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "✓ abi_history.yaml current entry (abi=${hist_abi}, api=${hist_api}) matches the header constants"
    exit 0
fi
exit 1
