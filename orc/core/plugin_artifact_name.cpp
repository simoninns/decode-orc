/*
 * File:        plugin_artifact_name.cpp
 * Module:      orc-core
 * Purpose:     Single source of truth for stage-plugin release-artifact naming:
 *              validation, ABI-tag parsing, and host-ABI-aware asset selection
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "include/plugin_artifact_name.h"

#include <regex>

namespace orc {
namespace {

constexpr const char* kAssetPrefix = "orc-plugin_";

// A single `<stage>_<platform>` segment (stage and platform each allow the same
// character class; the split between them is not needed for validation).
constexpr const char* kSegmentChars = "[A-Za-z0-9._-]+";

}  // namespace

ParsedReleaseAssetName parse_release_asset_name(const std::string& name) {
  ParsedReleaseAssetName parsed;

  // Overall structure: prefix + at least two `_`-separated segments + optional
  // `_abi<N>` + extension. The trailing extension is matched first so the
  // optional ABI tag can be extracted from the stem without greedy ambiguity.
  static const std::regex ext_regex(R"(\.(so|dylib|dll)$)");
  std::smatch ext_match;
  if (!std::regex_search(name, ext_match, ext_regex)) {
    return parsed;
  }
  const std::string ext = ext_match[1].str();
  std::string stem = name.substr(0, name.size() - (ext.size() + 1));

  bool has_abi = false;
  uint32_t abi = 0;
  static const std::regex abi_regex(R"(_abi([0-9]+)$)");
  std::smatch abi_match;
  if (std::regex_search(stem, abi_match, abi_regex)) {
    has_abi = true;
    abi = static_cast<uint32_t>(std::stoul(abi_match[1].str()));
    stem = stem.substr(0, stem.size() - abi_match[0].length());
  }

  // The remaining stem must be orc-plugin_<stage>_<platform> with two non-empty
  // segments.
  static const std::regex stem_regex(std::string("^") + kAssetPrefix +
                                     kSegmentChars + "_" + kSegmentChars + "$");
  if (!std::regex_match(stem, stem_regex)) {
    return parsed;
  }

  parsed.valid = true;
  parsed.ext = ext;
  parsed.has_abi = has_abi;
  parsed.abi = abi;
  return parsed;
}

bool is_valid_release_asset_name(const std::string& name) {
  return parse_release_asset_name(name).valid;
}

std::string platform_artifact_extension(const std::string& target_platform) {
  if (target_platform == "windows") {
    return ".dll";
  }
  if (target_platform == "macos") {
    return ".dylib";
  }
  return ".so";
}

std::string platform_artifact_token(const std::string& target_platform) {
  if (target_platform == "windows") {
    return "_windows";
  }
  if (target_platform == "macos") {
    return "_macos";
  }
  return "_linux";
}

ReleaseAssetSelection select_release_asset(
    const std::vector<ReleaseAssetCandidate>& candidates,
    const std::string& target_platform, uint32_t host_abi) {
  ReleaseAssetSelection selection;

  const std::string required_ext = platform_artifact_extension(target_platform);
  const std::string preferred_token = platform_artifact_token(target_platform);

  int best_index = -1;
  int best_score = 0;
  bool best_has_token = false;
  bool best_abi_ok = false;
  bool best_abi_bad = false;

  for (size_t i = 0; i < candidates.size(); ++i) {
    const ReleaseAssetCandidate& candidate = candidates[i];
    const ParsedReleaseAssetName parsed =
        parse_release_asset_name(candidate.name);
    if (!parsed.valid) {
      continue;
    }
    if ("." + parsed.ext != required_ext) {
      continue;
    }

    const bool has_token =
        candidate.name.find(preferred_token) != std::string::npos;
    const bool abi_ok = parsed.has_abi && parsed.abi == host_abi;
    const bool abi_bad = parsed.has_abi && parsed.abi != host_abi;

    // Platform correctness dominates; ABI correctness is the tie-breaker;
    // a wrong ABI tag is only ever a last resort.
    int score = 0;
    score += has_token ? 100 : 0;
    if (abi_ok) {
      score += 50;
    } else if (abi_bad) {
      score -= 1000;
    } else {
      score += 10;  // legacy untagged: acceptable, load-time gate validates it
    }

    // best_index < 0 means no candidate has matched prefix+ext yet, so the
    // first match always wins regardless of its (possibly negative) score.
    if (best_index < 0 || score > best_score) {
      best_score = score;
      best_index = static_cast<int>(i);
      best_has_token = has_token;
      best_abi_ok = abi_ok;
      best_abi_bad = abi_bad;
    }
  }

  selection.index = best_index;
  if (best_index >= 0) {
    selection.missing_platform_token = !best_has_token;
    selection.used_legacy_untagged = !best_abi_ok && !best_abi_bad;
    selection.abi_mismatch = best_abi_bad;
  }
  return selection;
}

}  // namespace orc
