/*
 * File:        plugin_remote_loader.h
 * Module:      orc-core
 * Purpose:     Utilities for downloading and caching remote plugin binaries
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#if defined(ORC_GUI_BUILD)
#error \
    "GUI code cannot include core/include/plugin_remote_loader.h. Use ProjectPresenter for plugin-aware stage access."
#endif
#if defined(ORC_CLI_BUILD)
#error \
    "CLI code cannot include core/include/plugin_remote_loader.h. Use ProjectPresenter for plugin-aware stage access."
#endif

#include <optional>
#include <string>
#include <vector>

namespace orc {

class PluginRemoteLoader {
 public:
  struct ResolveReleaseAssetResult {
    bool success = false;
    std::string source_repo_url;
    std::string release_tag;
    std::string release_asset_url;
    std::string release_asset_name;
    std::string error_message;
  };

  struct DownloadResult {
    bool success = false;
    std::string downloaded_path;
    std::string error_message;
  };

  /**
   * Download a plugin binary from a GitHub release asset URL.
   *
   * @param github_release_url Full GitHub API URL to the release asset
   *        Example:
   * https://api.github.com/repos/owner/repo/releases/download/v1.0.0/plugin.so
   * @param asset_name Expected filename (must match
   * orc-plugin_<stage>_<platform>.<ext> pattern)
   * @param target_platform Platform identifier (linux, macos, windows)
   * @param warnings Optional warning accumulator for informational messages
   *
   * @return DownloadResult with success flag, local path (if success), and
   * error message (if failure)
   *
   * On success, the binary is cached in
   * ~/.config/decode-orc/plugin-cache/<platform>/ Subsequent calls with the
   * same URL/asset_name retrieve the cached copy.
   */
  static DownloadResult download_release_asset(
      const std::string& github_release_url, const std::string& asset_name,
      const std::string& target_platform,
      std::vector<std::string>* warnings = nullptr);

  /**
   * Resolve a GitHub releases URL to a concrete release asset for the target
   * platform.
   *
   * Accepted inputs include:
   * - https://github.com/<owner>/<repo>/releases
   * - https://github.com/<owner>/<repo>/releases/tag/<tag>
   */
  static ResolveReleaseAssetResult resolve_release_asset_from_releases_url(
      const std::string& releases_url, const std::string& target_platform,
      std::vector<std::string>* warnings = nullptr);

  /**
   * Resolve the cache directory for downloaded plugins.
   * Creates directory if it doesn't exist.
   *
   * @param error Optional error message accumulator
   * @return Cache directory path, or empty string on failure
   */
  static std::string resolve_plugin_cache_dir(std::string* error = nullptr);

  /**
   * Get the local cache path for a given asset without downloading.
   *
   * @param asset_name Expected filename
   * @param target_platform Platform identifier
   * @return Absolute path where asset would be cached
   */
  static std::string get_cache_path(const std::string& asset_name,
                                    const std::string& target_platform,
                                    const std::string& release_tag = "");
};

}  // namespace orc
