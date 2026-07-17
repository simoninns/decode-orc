/*
 * File:        plugin_artifact_name.h
 * Module:      orc-core
 * Purpose:     Single source of truth for stage-plugin release-artifact naming:
 *              validation, ABI-tag parsing, and host-ABI-aware asset selection
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orc {

// Naming convention for a stage-plugin release artifact:
//
//   orc-plugin_<stage>_<platform>[_abi<N>].<so|dylib|dll>
//
// The optional `_abi<N>` token lets one plugin release ship builds for several
// host ABI versions side by side; legacy (untagged) names remain valid.

/// Parsed components of a release-asset filename.
struct ParsedReleaseAssetName {
  bool valid = false;    ///< true when the name matches the convention
  std::string ext;       ///< platform extension without the dot: so/dylib/dll
  bool has_abi = false;  ///< true when an `_abi<N>` token is present
  uint32_t abi = 0;      ///< the parsed ABI number when has_abi is true
};

/// Parse and validate a release-asset filename against the naming convention.
/// Accepts both the legacy `orc-plugin_<stage>_<platform>.<ext>` form and the
/// ABI-tagged `orc-plugin_<stage>_<platform>_abi<N>.<ext>` form.
ParsedReleaseAssetName parse_release_asset_name(const std::string& name);

/// Convenience wrapper: true when the name is a valid release-asset filename.
bool is_valid_release_asset_name(const std::string& name);

/// Platform-specific shared-library extension including the leading dot,
/// e.g. ".so" / ".dylib" / ".dll". Unknown platforms default to ".so".
std::string platform_artifact_extension(const std::string& target_platform);

/// Platform token embedded in artifact names, e.g. "_linux" / "_macos" /
/// "_windows". Unknown platforms default to "_linux".
std::string platform_artifact_token(const std::string& target_platform);

/// A downloadable release asset considered during selection.
struct ReleaseAssetCandidate {
  std::string url;
  std::string name;
};

/// Outcome flags describing how selection resolved, for caller-side warnings.
struct ReleaseAssetSelection {
  int index = -1;  ///< index into the candidate vector, or -1 if none matched
  bool missing_platform_token = false;  ///< chosen asset lacks the platform tag
  bool used_legacy_untagged = false;    ///< chosen asset carries no `_abi<N>`
  bool abi_mismatch = false;  ///< chosen asset is tagged for another ABI
};

/// Choose the best release asset for the given platform and host ABI.
///
/// Preference order (highest first), among candidates carrying the
/// `orc-plugin_` prefix and the platform's extension:
///   1. platform token present and `_abi<host_abi>` tag        (ideal)
///   2. platform token present and no ABI tag                  (legacy)
///   3. no platform token and `_abi<host_abi>` tag
///   4. no platform token and no ABI tag
///   5. an ABI tag for a different version                     (last resort)
/// Ties resolve to the earliest candidate (GitHub returns assets in a stable
/// order). Returns index -1 when nothing matches prefix + extension.
ReleaseAssetSelection select_release_asset(
    const std::vector<ReleaseAssetCandidate>& candidates,
    const std::string& target_platform, uint32_t host_abi);

}  // namespace orc
