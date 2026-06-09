/*
 * File:        stage_plugin_loader_version_rejection_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Verify that StagePluginLoader rejects plugins with mismatched
 *              ABI / API version numbers before any registration occurs.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "../../../orc/core/include/stage_plugin_loader.h"

namespace orc_unit_test {
namespace {

std::filesystem::path test_fixture_path(const std::string& base_name) {
#if defined(_WIN32)
  return std::filesystem::path(ORC_TEST_FIXTURE_PLUGIN_BUILD_DIR) /
         ("orc-test-fixture-" + base_name + ".dll");
#elif defined(__APPLE__)
  return std::filesystem::path(ORC_TEST_FIXTURE_PLUGIN_BUILD_DIR) /
         ("liborc-test-fixture-" + base_name + ".dylib");
#else
  return std::filesystem::path(ORC_TEST_FIXTURE_PLUGIN_BUILD_DIR) /
         ("liborc-test-fixture-" + base_name + ".so");
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// Non-existent path
// ---------------------------------------------------------------------------

TEST(StagePluginLoaderRejectionTest, nonExistentPathReportsError) {
  orc::StagePluginLoader loader;
  const auto result = loader.load_plugin(
      "/definitely/does/not/exist/plugin.so",
      [](const std::string&, orc::StagePluginLoader::RegisterStageFactory) {
        return true;
      });

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
  EXPECT_FALSE(result.plugin.has_value());
}

// ---------------------------------------------------------------------------
// ABI version mismatch
// ---------------------------------------------------------------------------

TEST(StagePluginLoaderRejectionTest, wrongHostAbiVersionIsRejected) {
  const auto plugin_path = test_fixture_path("bad_abi_version");
  if (!std::filesystem::exists(plugin_path)) {
    GTEST_SKIP() << "Test fixture not built: " << plugin_path.string();
  }

  orc::StagePluginLoader loader;
  bool register_called = false;

  const auto result = loader.load_plugin(
      plugin_path.string(),
      [&register_called](const std::string&,
                         orc::StagePluginLoader::RegisterStageFactory) {
        register_called = true;
        return true;
      });

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
  EXPECT_FALSE(result.plugin.has_value());
  // Registration callback must never be invoked for a rejected plugin
  EXPECT_FALSE(register_called);
  // Error message should mention the mismatch
  EXPECT_NE(result.error_message.find("ABI"), std::string::npos)
      << "Expected 'ABI' in error: " << result.error_message;
}

// ---------------------------------------------------------------------------
// Plugin API version mismatch
// ---------------------------------------------------------------------------

TEST(StagePluginLoaderRejectionTest, wrongPluginApiVersionIsRejected) {
  const auto plugin_path = test_fixture_path("bad_api_version");
  if (!std::filesystem::exists(plugin_path)) {
    GTEST_SKIP() << "Test fixture not built: " << plugin_path.string();
  }

  orc::StagePluginLoader loader;
  bool register_called = false;

  const auto result = loader.load_plugin(
      plugin_path.string(),
      [&register_called](const std::string&,
                         orc::StagePluginLoader::RegisterStageFactory) {
        register_called = true;
        return true;
      });

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
  EXPECT_FALSE(result.plugin.has_value());
  EXPECT_FALSE(register_called);
  // Error message should mention the version mismatch
  EXPECT_TRUE(result.error_message.find("API version") != std::string::npos ||
              result.error_message.find("api_version") != std::string::npos)
      << "Expected API version mismatch in error: " << result.error_message;
}

}  // namespace orc_unit_test
