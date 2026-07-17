#!/usr/bin/env bash
# File:        check_plugin_private_links.sh
# Purpose:     Scan plugin build files for direct private host link-target coupling.
#              This is a hard enforcement gate; violations fail the build.
#
# Usage:       check_plugin_private_links.sh [<root>]
#              In-tree mode (<root> is a decode-orc checkout): scans
#              <root>/orc/plugins/stages.
#              Standalone mode (that directory does not exist under <root>):
#              scans every CMakeLists.txt under <root> itself — this is how
#              third-party plugin authors run the gate against their own
#              repository.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

REPO_ROOT="${1:-.}"

PRIVATE_TARGETS_REGEX='(orc-core|orc-common|orc-presenters|orc-gui|orc-cli|orc-metadata|orc-view-types)'
VIOLATIONS=0
FILES_WITH_VIOLATIONS=()

scan_file() {
    local file_path="$1"
    local rel_path="${file_path#${REPO_ROOT}/}"
    local file_violations=0

    # Detect direct target_link_libraries() on stage plugin targets.
    while IFS= read -r line; do
        if printf '%s\n' "$line" | grep -Eq "target_link_libraries\(orc-stage-plugin-[^[:space:])]+.*${PRIVATE_TARGETS_REGEX}"; then
            if [[ "$file_violations" -eq 0 ]]; then
                echo "❌ ${rel_path}:"
            fi
            echo "   direct plugin target links private host target: ${line#${line%%[![:space:]]*}}"
            file_violations=$((file_violations + 1))
            VIOLATIONS=$((VIOLATIONS + 1))
        fi
    done < "$file_path"

    # Detect private host targets in orc_add_stage_plugin(... LINK_LIBRARIES ...).
    while IFS= read -r hit; do
        if [[ "$file_violations" -eq 0 ]]; then
            echo "❌ ${rel_path}:"
        fi
        echo "   orc_add_stage_plugin LINK_LIBRARIES contains private host target: ${hit#*:}"
        file_violations=$((file_violations + 1))
        VIOLATIONS=$((VIOLATIONS + 1))
    done < <(
        awk '
            /orc_add_stage_plugin\(/ { in_plugin=1 }
            in_plugin && /LINK_LIBRARIES/ { in_links=1; next }
            in_plugin && /\)/ { in_plugin=0; in_links=0 }
            in_links { print FNR ":" $0 }
        ' "$file_path" | grep -E "$PRIVATE_TARGETS_REGEX" || true
    )

    if [[ "$file_violations" -gt 0 ]]; then
        FILES_WITH_VIOLATIONS+=("$rel_path")
    fi
}

echo "Scanning plugin build files for private host link targets..."
echo

if [[ -d "$REPO_ROOT/orc/plugins/stages" ]]; then
    # In-tree mode: decode-orc checkout.
    while IFS= read -r cmake_file; do
        scan_file "$cmake_file"
    done < <(find "$REPO_ROOT/orc/plugins/stages" -name CMakeLists.txt -type f | sort)
else
    # Standalone mode: <root> is a single external plugin tree.
    while IFS= read -r cmake_file; do
        scan_file "$cmake_file"
    done < <(find "$REPO_ROOT" -name CMakeLists.txt -type f \
        ! -path '*/build/*' ! -path '*/.git/*' | sort)
fi

echo
if [[ "$VIOLATIONS" -eq 0 ]]; then
    echo "✓ Plugin build files are SDK-clean (no direct private host link targets detected)"
    exit 0
fi

echo "❌ SDK ENFORCEMENT FAILURE: Found $VIOLATIONS direct private host link-target reference(s) in plugin build files"
echo
echo "   Affected files: $(IFS=,; echo "${FILES_WITH_VIOLATIONS[*]}")"
echo
echo "   SDK-only violations must be fixed before this build can proceed:"
echo "   - Route plugin targets through orc-plugin-sdk / orc::plugin-sdk only"
echo "   - Keep plugin-local shared libraries free of private host link targets"
echo "   - Remove direct host-library coupling from third-party build scripts"
echo
exit 1