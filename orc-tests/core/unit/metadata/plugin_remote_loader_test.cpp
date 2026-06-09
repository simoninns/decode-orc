/*
 * File:        plugin_remote_loader_test.cpp
 * Module:      orc-core unit tests
 * Purpose:     Unit tests for remote plugin binary download and caching
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../orc/core/include/plugin_remote_loader.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace orc_unit_test {

// Note: Full end-to-end download testing requires libcurl and network access.
// These unit tests focus on validation and cache path logic.
// Integration tests with actual downloads to GitHub releases will be added to
// the test suite.

TEST(PluginRemoteLoaderTest, getCachePathFormsCorrectPlatformSubdirectory) {
  const auto path = orc::PluginRemoteLoader::get_cache_path(
      "orc-plugin_skeleton_passthrough_linux.so", "linux");
  EXPECT_TRUE(path.find("plugin-cache") != std::string::npos);
  EXPECT_TRUE(path.find("linux") != std::string::npos);
  EXPECT_TRUE(path.find("orc-plugin_skeleton_passthrough_linux.so") !=
              std::string::npos);
}

TEST(PluginRemoteLoaderTest, getCachePathFormsCorrectWindowsPath) {
  const auto path = orc::PluginRemoteLoader::get_cache_path(
      "orc-plugin_skeleton_passthrough_windows.dll", "windows");
  EXPECT_TRUE(path.find("plugin-cache") != std::string::npos);
  EXPECT_TRUE(path.find("windows") != std::string::npos);
  EXPECT_TRUE(path.find("orc-plugin_skeleton_passthrough_windows.dll") !=
              std::string::npos);
}

TEST(PluginRemoteLoaderTest, downloadRejectsInvalidAssetNames) {
  std::vector<std::string> warnings;

  // Missing stage and platform (no underscore separators)
  auto result = orc::PluginRemoteLoader::download_release_asset(
      "https://api.github.com/repos/example/repo/releases/download/v1.0.0/"
      "invalid.so",
      "invalid.so", "linux", &warnings);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error_message.find("Invalid asset name format") !=
              std::string::npos);

  // Missing extension
  result = orc::PluginRemoteLoader::download_release_asset(
      "https://api.github.com/repos/example/repo/releases/download/v1.0.0/"
      "orc-plugin_stage_linux",
      "orc-plugin_stage_linux", "linux", &warnings);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error_message.find("Invalid asset name format") !=
              std::string::npos);

  // Wrong extension
  result = orc::PluginRemoteLoader::download_release_asset(
      "https://api.github.com/repos/example/repo/releases/download/v1.0.0/"
      "orc-plugin_stage_linux.txt",
      "orc-plugin_stage_linux.txt", "linux", &warnings);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error_message.find("Invalid asset name format") !=
              std::string::npos);

  // Valid format should pass validation (may fail on network, but not format
  // validation)
  result = orc::PluginRemoteLoader::download_release_asset(
      "https://invalid.example.test/releases/download/v1.0.0/"
      "orc-plugin_stage_linux.so",
      "orc-plugin_stage_linux.so", "linux", &warnings);
  // Format validation should pass (curl connection error is OK)
  EXPECT_TRUE(result.error_message.find("Invalid asset name format") ==
              std::string::npos);
}

}  // namespace orc_unit_test
