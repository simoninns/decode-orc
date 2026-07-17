#!/usr/bin/env bash
# File:        check_abi_bump.sh
# Purpose:     CI bump-procedure guard for the stage-plugin ABI. Fails when:
#                (1) the header ABI/API constants are out of sync with
#                    orc/sdk/abi_history.yaml (delegates to check_abi_history.sh),
#                (2) the generated version-history table in plugin-sdk.md is
#                    stale (delegates to check_abi_history_docs.sh), or
#                (3) a non-deprecated orc/abi/ or orc/stage/ *contract* header
#                    changed versus the base ref without a matching edit to
#                    orc/sdk/abi_history.yaml (i.e. neither an ABI bump nor an
#                    explicit abi-neutral acknowledgement was recorded).
#
#              On any failure it prints the ABI-bump checklist so contributors
#              know exactly what to update.
#
# Usage:       tools/check_abi_bump.sh [<repo-root>]
#              BASE_REF=<git-ref> tools/check_abi_bump.sh   # override base
#
# The contract-header drift check (3) is git-based and skips gracefully when no
# base ref can be resolved (shallow clone, detached first commit, non-git tree).
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -u
REPO_ROOT="${1:-.}"
CMAKE_DIR="${REPO_ROOT}/cmake"
MANIFEST="${REPO_ROOT}/orc/sdk/sdk_headers.yaml"
HISTORY_REL="orc/sdk/abi_history.yaml"

print_checklist() {
    cat <<'EOF'

  ── ABI-bump checklist ─────────────────────────────────────────────────────
  When you change the stage-plugin ABI, update all of the following together:
    1. orc/sdk/include/orc/abi/orc_plugin_abi.h — bump kStagePluginHostAbiVersion
       (and ORC_SDK_ABI_VERSION) and/or kStagePluginApiVersion.
    2. orc/sdk/abi_history.yaml — append an entry (abi, api, cause, contracts,
       summary) whose abi/api match the new header constants.
    3. docs/technical/plugin-sdk.md — regenerate the version-history table:
         tools/gen_abi_history_docs.sh . (splice between the GENERATED ABI
         VERSION HISTORY markers).
    4. docs/technical/plugin-architecture.md — update the host_abi_version
       "Current value" note.

  If you changed an orc/abi/ or orc/stage/ contract header WITHOUT a binary-
  incompatible change (an abi-neutral edit — comments, additive non-vtable
  helpers, etc.), still record it: edit orc/sdk/abi_history.yaml (e.g. extend
  the current entry's notes) so the change is explicitly acknowledged.

  Unsure whether a change forces a bump? See the ABI impact decision table:
    docs/technical/plugin-sdk.md#abi-impact-decision-table
  ────────────────────────────────────────────────────────────────────────────
EOF
}

fail=0

# (1) + (2) — the always-on sync checks.
if ! bash "${CMAKE_DIR}/check_abi_history.sh" "$REPO_ROOT"; then fail=1; fi
if ! bash "${CMAKE_DIR}/check_abi_history_docs.sh" "$REPO_ROOT"; then fail=1; fi

# (3) — contract-header drift versus a base ref.
resolve_base() {
    if [ -n "${BASE_REF:-}" ]; then echo "$BASE_REF"; return 0; fi
    if git -C "$REPO_ROOT" rev-parse --verify -q origin/main >/dev/null; then
        echo "origin/main"; return 0
    fi
    if git -C "$REPO_ROOT" rev-parse --verify -q HEAD~1 >/dev/null; then
        echo "HEAD~1"; return 0
    fi
    return 1
}

if ! git -C "$REPO_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    echo "ℹ check_abi_bump: not a git tree; skipping contract-header drift check"
elif ! base="$(resolve_base)"; then
    echo "ℹ check_abi_bump: no base ref resolvable; skipping contract-header drift check"
else
    changed="$(git -C "$REPO_ROOT" diff --name-only "$base" -- \
                 'orc/sdk/include/orc/abi' 'orc/sdk/include/orc/stage' \
                 "$HISTORY_REL" 2>/dev/null)"

    # Deprecated shim paths are not contract surface; exclude them.
    deprecated_paths="$(awk '
        /^[[:space:]]*-[[:space:]]*path:/ { p=$0; sub(/^[^:]*:[[:space:]]*/,"",p); pend=1 }
        /^[[:space:]]*deprecated:[[:space:]]*true/ { if (pend) print "orc/sdk/include/" p; pend=0 }
        /^[[:space:]]*deprecated:[[:space:]]*false/ { pend=0 }
    ' "$MANIFEST" 2>/dev/null)"

    history_touched=0
    contract_changed=""
    while IFS= read -r f; do
        [ -z "$f" ] && continue
        if [ "$f" = "$HISTORY_REL" ]; then history_touched=1; continue; fi
        # Skip deprecated shims.
        skip=0
        while IFS= read -r d; do
            [ -z "$d" ] && continue
            if [ "$f" = "$d" ]; then skip=1; break; fi
        done <<EOF
$deprecated_paths
EOF
        [ "$skip" -eq 1 ] && continue
        contract_changed="${contract_changed}${f}"$'\n'
    done <<EOF
$changed
EOF

    if [ -n "$contract_changed" ] && [ "$history_touched" -eq 0 ]; then
        echo "❌ contract header(s) changed vs ${base} without an abi_history.yaml edit:"
        printf '%s' "$contract_changed" | sed 's/^/     /'
        echo "   Record the change: bump the ABI, or add an abi-neutral note to"
        echo "   orc/sdk/abi_history.yaml, so the edit is explicitly acknowledged."
        fail=1
    else
        echo "✓ no unacknowledged contract-header drift vs ${base}"
    fi
fi

if [ "$fail" -ne 0 ]; then
    print_checklist
    exit 1
fi
echo "✓ ABI bump procedure checks passed"
exit 0
