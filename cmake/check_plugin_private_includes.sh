#!/usr/bin/env bash
# File:        check_plugin_private_includes.sh
# Purpose:     Allowlist gate: every #include in stage plugin code must be an
#              allowlisted SDK contract header, a plugin-local header, a
#              standard-library/platform header, or a permitted third-party
#              header. Anything else fails the build (hard enforcement gate).
#
# Usage:       check_plugin_private_includes.sh [<root>]
#              In-tree mode (<root> is a decode-orc checkout): scans
#              <root>/orc/plugins/stages and <root>/3rd-party-plugins.
#              Standalone mode (neither directory exists under <root>):
#              scans <root> itself as a single external plugin tree — this
#              is how third-party plugin authors run the gate against their
#              own repository.
#
# The SDK header allowlist below is the contract surface documented in
# docs/technical/plugin-sdk.md (SDK Headers section). Adding a header to the
# SDK requires updating both that document and this list — that is
# deliberate: the allowlist is the enforced contract.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -u

REPO_ROOT="${1:-.}"
PLUGIN_STAGES_DIR="${REPO_ROOT}/orc/plugins/stages"
THIRD_PARTY_DIR="${REPO_ROOT}/3rd-party-plugins"

# Standalone mode: <root> is a single external plugin tree, not a decode-orc
# checkout. The whole tree is one plugin owner.
STANDALONE_TREE=0
if [ ! -d "$PLUGIN_STAGES_DIR" ] && [ ! -d "$THIRD_PARTY_DIR" ]; then
    STANDALONE_TREE=1
fi

# ---------------------------------------------------------------------------
# Allowlisted SDK contract headers (<orc/plugin/...> and <orc/stage/...>)
# ---------------------------------------------------------------------------
SDK_ALLOWLIST=(
    # Plugin ABI / services surface
    "orc/plugin/orc_plugin_abi.h"
    "orc/plugin/orc_plugin_registration.h"
    "orc/plugin/orc_plugin_sdk.h"
    "orc/plugin/orc_plugin_services.h"
    "orc/plugin/orc_plugin_services_helpers.h"
    "orc/plugin/orc_stage_api.h"
    "orc/plugin/orc_stage_preview.h"
    "orc/plugin/orc_stage_runtime.h"
    "orc/plugin/orc_stage_services.h"
    "orc/plugin/orc_stage_tooling.h"
    # Stage model
    "orc/stage/stage.h"
    "orc/stage/triggerable_stage.h"
    "orc/stage/stage_parameter.h"
    "orc/stage/parameter_types.h"
    "orc/stage/node_type.h"
    "orc/stage/node_id.h"
    "orc/stage/artifact.h"
    "orc/stage/analysis_sink_results.h"
    # Frame / signal model
    "orc/stage/audio_track.h"
    "orc/stage/video_frame_representation.h"
    "orc/stage/video_metadata_types.h"
    "orc/stage/frame_descriptor.h"
    "orc/stage/frame_id.h"
    "orc/stage/field_id.h"
    "orc/stage/frame_line_util.h"
    "orc/stage/common_types.h"
    "orc/stage/cvbs_signal_constants.h"
    "orc/stage/orc_source_parameters.h"
    "orc/stage/dropout_run.h"
    "orc/stage/dropout_util.h"
    "orc/stage/dropout_decision.h"
    # Observation model
    "orc/stage/observation_context.h"
    "orc/stage/observation_context_interface.h"
    "orc/stage/observation_schema.h"
    "orc/stage/observers/observer.h"
    "orc/stage/observers/biphase_observer.h"
    "orc/stage/observers/black_psnr_observer.h"
    "orc/stage/observers/burst_level_observer.h"
    "orc/stage/observers/closed_caption_observer.h"
    "orc/stage/observers/white_snr_observer.h"
    # Preview contract
    "orc/stage/stage_preview_capability.h"
    "orc/stage/stage_custom_preview_renderer.h"
    "orc/stage/colour_preview_provider.h"
    "orc/stage/colour_preview_conversion.h"
    "orc/stage/preview_helpers.h"
    "orc/stage/preview_stage_types.h"
    "orc/stage/orc_preview_types.h"
    "orc/stage/orc_preview_carriers.h"
    "orc/stage/orc_rendering.h"
    "orc/stage/orc_vectorscope.h"
    # Utilities
    "orc/stage/logging.h"
    "orc/stage/lru_cache.h"
    "orc/stage/file_io_interface.h"
    "orc/stage/eia608_decoder.h"
    "orc/stage/error_types.h"
)

# C standard-library headers (C++ headers are matched by pattern: no '.'
# and no '/' in an angle include, e.g. <vector>, <filesystem>).
C_STD_HEADERS=(
    "assert.h" "complex.h" "ctype.h" "errno.h" "fenv.h" "float.h"
    "inttypes.h" "iso646.h" "limits.h" "locale.h" "math.h" "setjmp.h"
    "signal.h" "stdalign.h" "stdarg.h" "stdatomic.h" "stdbool.h" "stddef.h"
    "stdint.h" "stdio.h" "stdlib.h" "stdnoreturn.h" "string.h" "tgmath.h"
    "threads.h" "time.h" "uchar.h" "wchar.h" "wctype.h"
)

# Platform headers plugins may use (POSIX / Windows file I/O, dlopen).
PLATFORM_HEADERS=(
    "unistd.h" "fcntl.h" "io.h" "share.h" "windows.h" "dlfcn.h"
)

# Permitted third-party header prefixes/names. fmt and spdlog are permitted
# unconditionally (the SDK logging surface uses them); the rest are
# plugin-declared dependencies (each plugin's CMakeLists.txt links the
# library itself).
THIRD_PARTY_PATTERNS=(
    "fmt/*" "spdlog/*"          # SDK logging surface
    "ezpwd/*"                   # efm_sink Reed-Solomon
    "libavcodec/*" "libavfilter/*" "libavformat/*" "libavutil/*"
    "libswscale/*" "libswresample/*"  # ffmpeg video sink
    "fftw3.h"                   # Transform PAL chroma decoder
    "sqlite3.h"                 # tbc_source / ld_sink / cvbs_source metadata
    "soxr.h"                    # tbc_source audio resampling
    "sys/*"                     # POSIX
)

VIOLATIONS=0
STAGE_VIOLATIONS=()

in_list() {
    local needle="$1"
    shift
    local item
    for item in "$@"; do
        [ "$needle" = "$item" ] && return 0
    done
    return 1
}

matches_pattern_list() {
    local needle="$1"
    shift
    local pattern
    for pattern in "$@"; do
        # shellcheck disable=SC2254
        case "$needle" in
            $pattern) return 0 ;;
        esac
    done
    return 1
}

# Determine the plugin owner directory for a source file: the top-level
# entry under the scan root, except for the sources/ and sinks/ family
# subtrees where the owner is one level deeper (sinks/common is its own
# owner: the sanctioned shared implementation for the video sinks).
owner_dir_for() {
    local src_file="$1"
    local root_dir="$2"
    # Standalone external plugin tree: the whole tree is one owner.
    if [ "$STANDALONE_TREE" = "1" ]; then
        printf '%s' "$root_dir"
        return
    fi
    local rel="${src_file#"$root_dir"/}"
    local first="${rel%%/*}"
    if [ "$first" = "sources" ] || [ "$first" = "sinks" ]; then
        local second
        second="$(printf '%s' "$rel" | cut -d'/' -f2)"
        printf '%s/%s/%s' "$root_dir" "$first" "$second"
    else
        printf '%s/%s' "$root_dir" "$first"
    fi
}

# Does <include_path> resolve to a file inside <tree>? Checks (1) relative
# to the including file's directory (handles "../..." spellings), then
# (2) any path-suffix match inside the tree (handles per-target include
# directories such as efm-lib/ or the plugin root itself).
# (No bash-4 features here: external plugin authors run this standalone,
# including on macOS's bash 3.2.)
resolves_in_tree() {
    local include_path="$1"
    local including_dir="$2"
    local tree="$3"

    [ -d "$tree" ] || return 1

    local candidate
    candidate="$(realpath -m "${including_dir}/${include_path}" 2>/dev/null)" || candidate=""
    if [ -n "$candidate" ] && [ -f "$candidate" ]; then
        local tree_abs
        tree_abs="$(realpath -m "$tree")"
        case "$candidate" in
            "$tree_abs"/*) return 0 ;;
        esac
    fi

    # Suffix match against the tree's file list.
    local stripped="${include_path#./}"
    while [ "${stripped#../}" != "$stripped" ]; do
        stripped="${stripped#../}"
    done
    [ -n "$stripped" ] || return 1
    if [ -n "$(find "$tree" -type f -path "*/${stripped}" -print -quit 2>/dev/null)" ]; then
        return 0
    fi
    return 1
}

# Classify one include directive. Returns 0 if allowed, 1 with a reason on
# stdout if forbidden.
check_include() {
    local include_path="$1"
    local including_dir="$2"
    local owner_dir="$3"
    local root_dir="$4"

    case "$include_path" in
        orc/plugin/*|orc/stage/*)
            if in_list "$include_path" "${SDK_ALLOWLIST[@]}"; then
                return 0
            fi
            echo "not an allowlisted SDK contract header"
            return 1
            ;;
        orc/*)
            echo "not an allowlisted SDK contract header"
            return 1
            ;;
    esac

    # C++ standard library: angle-style name with no '.' and no '/'
    # (<vector>, <filesystem>, ...).
    case "$include_path" in
        */*) : ;;
        *.*) : ;;
        *) return 0 ;;
    esac

    if in_list "$include_path" "${C_STD_HEADERS[@]}"; then
        return 0
    fi
    if in_list "$include_path" "${PLATFORM_HEADERS[@]}"; then
        return 0
    fi
    if matches_pattern_list "$include_path" "${THIRD_PARTY_PATTERNS[@]}"; then
        return 0
    fi

    # Plugin-local headers: must resolve inside the owning plugin's tree.
    if resolves_in_tree "$include_path" "$including_dir" "$owner_dir"; then
        return 0
    fi

    # Sanctioned shared trees:
    #   - sink plugins share orc/plugins/stages/sinks/common/
    #   - ld_sink shares tbc_source's ld-decode metadata structures
    case "$owner_dir" in
        */sinks/*)
            if resolves_in_tree "$include_path" "$including_dir" \
                "${root_dir}/sinks/common"; then
                return 0
            fi
            ;;
    esac
    case "$owner_dir" in
        */ld_sink)
            if resolves_in_tree "$include_path" "$including_dir" \
                "${root_dir}/tbc_source"; then
                return 0
            fi
            ;;
    esac

    echo "not allowlisted (private host header, or undeclared dependency)"
    return 1
}

scan_tree() {
    local root_dir="$1"
    local label_suffix="$2"

    [ -d "$root_dir" ] || return 0

    while IFS= read -r src_file; do
        local src_violation_count=0
        local rel_file="${src_file#"$REPO_ROOT"/}"
        local including_dir
        including_dir="$(dirname "$src_file")"
        local owner_dir
        owner_dir="$(owner_dir_for "$src_file" "$root_dir")"
        local owner="${owner_dir#"$root_dir"/}"
        if [ "$owner" = "$owner_dir" ]; then
            # Standalone tree: owner is the root itself; report its basename.
            owner="$(basename "$owner_dir")"
        fi

        while IFS= read -r include_path; do
            [ -n "$include_path" ] || continue
            local reason
            if ! reason="$(check_include "$include_path" "$including_dir" \
                "$owner_dir" "$root_dir")"; then
                if [ "$src_violation_count" -eq 0 ]; then
                    echo "❌ ${owner}${label_suffix}:"
                fi
                echo "   ${rel_file}: includes ${include_path} — ${reason}"
                src_violation_count=$((src_violation_count + 1))
                VIOLATIONS=$((VIOLATIONS + 1))
                STAGE_VIOLATIONS+=("$owner")
            fi
        done < <(grep -hoE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">]+[">]' \
            "$src_file" 2>/dev/null \
            | sed -E 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]([^">]+)[">]/\1/')
    done < <(find "$root_dir" \
        -type f \
        \( -name '*.h' -o -name '*.hpp' -o -name '*.hh' -o -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) \
        ! -path '*/tests/*' \
        ! -path '*/build/*' \
        ! -path '*/.git/*')
}

if [ "$STANDALONE_TREE" = "1" ]; then
    echo "Scanning standalone plugin tree against the SDK include allowlist..."
    echo
    scan_tree "$REPO_ROOT" " (standalone)"
else
    echo "Scanning in-tree plugin code against the SDK include allowlist..."
    echo
    scan_tree "$PLUGIN_STAGES_DIR" ""

    echo
    echo "Scanning third-party plugin code against the SDK include allowlist..."
    echo
    scan_tree "$THIRD_PARTY_DIR" " (3rd-party)"
fi

echo
if [ "$VIOLATIONS" -eq 0 ]; then
    echo "✓ All plugin code is SDK-clean (every include is allowlisted)"
    exit 0
else
    echo "❌ HARD GATE FAILURE: Found $VIOLATIONS non-allowlisted include(s) across plugin code"
    echo
    echo "   Affected stages/plugins: $(printf '%s\n' "${STAGE_VIOLATIONS[@]}" | awk 'NF && !seen[$0]++' | paste -sd ',' - | sed 's/,/, /g')"
    echo
    echo "   Plugins may only include:"
    echo "   - Allowlisted SDK contract headers (<orc/plugin/...>, <orc/stage/...>)"
    echo "   - Headers inside the plugin's own directory"
    echo "   - C/C++ standard-library and platform headers"
    echo "   - Permitted third-party headers (fmt/spdlog; plugin-declared deps)"
    echo
    echo "   If an SDK capability is missing, expand the SDK contract first"
    echo "   (docs/technical/plugin-sdk.md) rather than bypassing it."
    echo
    exit 1  # Hard gate: fail CI on violations
fi
