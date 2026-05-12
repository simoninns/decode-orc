#!/usr/bin/env bash
# File:        check_plugin_private_includes.sh
# Purpose:     Scan stage plugin code for private (non-SDK) include dependencies.
#              This is a hard enforcement gate; violations fail the build.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

REPO_ROOT="${1:-.}"
PLUGIN_STAGES_DIR="${REPO_ROOT}/orc/plugins/stages"
THIRD_PARTY_DIR="${REPO_ROOT}/3rd-party-plugins"

# Private headers that should NOT be included in plugin-side code
PRIVATE_HEADERS=(
    "dag_executor.h"
    "stage_registry.h"
    "stage_plugin_loader.h"
    "stage_plugin_discovery.h"
    "previewable_stage.h"
    "stage_preview_capability.h"
    "colour_preview_provider.h"
    "observable_stage.h"
    "observation_context.h"
    "tbc_source_internal.h"
    "tbc_video_field_representation.h"
    "tbc_yc_video_field_representation.h"
    "preset_manager.h"
    "analysis_tools.h"
)

VIOLATIONS=0
STAGE_VIOLATIONS=()

scan_tree() {
    local root_dir="$1"
    local label_suffix="$2"

    [ -d "$root_dir" ] || return 0

    while IFS= read -r src_file; do
        local src_violation_count=0
        local rel_file="${src_file#"$REPO_ROOT"/}"
        local rel_from_scan_root
        rel_from_scan_root="${src_file#"$root_dir"/}"

        local owner
        owner="${rel_from_scan_root%%/*}"
        if [ "$owner" = "sources" ] || [ "$owner" = "sinks" ]; then
            owner="$(printf '%s' "$rel_from_scan_root" | cut -d'/' -f2)"
        fi

        for private_header in "${PRIVATE_HEADERS[@]}"; do
            local escaped_header
            escaped_header=$(printf '%s' "$private_header" | sed 's/[.[\*^$()+?{}|]/\\&/g')

            if grep -Eq "#include[[:space:]]*[\"<][^\">]*${escaped_header}" "$src_file"; then
                if [ "$src_violation_count" -eq 0 ]; then
                    echo "❌ ${owner}${label_suffix}:"
                fi
                echo "   ${rel_file}: includes ${private_header}"
                src_violation_count=$((src_violation_count + 1))
                VIOLATIONS=$((VIOLATIONS + 1))
                STAGE_VIOLATIONS+=("$owner")
            fi
        done
    done < <(find "$root_dir" \
        -type f \
        \( -name '*.h' -o -name '*.hpp' -o -name '*.hh' -o -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) \
        ! -path '*/tests/*' \
        ! -path '*/build/*' \
        ! -path '*/.git/*')
}

echo "Scanning in-tree plugin code for private includes..."
echo
scan_tree "$PLUGIN_STAGES_DIR" ""

echo
echo "Scanning third-party plugin code for private includes..."
echo
scan_tree "$THIRD_PARTY_DIR" " (3rd-party)"

echo
if [ "$VIOLATIONS" -eq 0 ]; then
    echo "✓ All plugin code is SDK-clean (no private includes detected)"
    exit 0
else
    echo "❌ HARD GATE FAILURE: Found $VIOLATIONS private header inclusion(s) across plugin code"
    echo
    echo "   Affected stages/plugins: $(printf '%s\n' "${STAGE_VIOLATIONS[@]}" | awk 'NF && !seen[$0]++' | paste -sd ',' - | sed 's/,/, /g')"
    echo
    echo "   SDK-only violations must be fixed before this build can proceed:"
    echo "   - Remove private includes from plugin headers and implementation files"
    echo "   - Use SDK-only interfaces in plugin APIs and helper code"
    echo "   - Keep private host internals out of plugin-stage trees"
    echo
    exit 1  # Hard gate: fail CI on violations
fi
