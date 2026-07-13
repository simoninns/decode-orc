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
#include <orc/plugin/orc_plugin_abi.h>

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

TEST(StagePluginLoaderRejectionTest, NonExistentPath_ReportsError) {
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

TEST(StagePluginLoaderRejectionTest, WrongHostAbiVersion_IsRejected) {
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

TEST(StagePluginLoaderRejectionTest, WrongPluginApiVersion_IsRejected) {
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

// ---------------------------------------------------------------------------
// Previous ABI revision (v4, pre-toolchain-tag)
// ---------------------------------------------------------------------------

TEST(StagePluginLoaderRejectionTest, AbiV4Plugin_IsRejectedByVersionCheck) {
  const auto plugin_path = test_fixture_path("abi_v4");
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
  EXPECT_FALSE(result.plugin.has_value());
  EXPECT_FALSE(register_called);
  // Rejected on the version number alone; the diagnostic names both sides.
  EXPECT_NE(result.error_message.find("ABI"), std::string::npos)
      << "Expected 'ABI' in error: " << result.error_message;
  EXPECT_NE(result.error_message.find("plugin=4"), std::string::npos)
      << "Expected plugin ABI 4 in error: " << result.error_message;
  const std::string expected_host =
      "host=" + std::to_string(orc::kStagePluginHostAbiVersion);
  EXPECT_NE(result.error_message.find(expected_host), std::string::npos)
      << "Expected '" << expected_host
      << "' in error: " << result.error_message;
}

// ---------------------------------------------------------------------------
// Toolchain tag mismatch
// ---------------------------------------------------------------------------

TEST(StagePluginLoaderRejectionTest, WrongToolchainTag_IsRejectedNamingBoth) {
  const auto plugin_path = test_fixture_path("bad_toolchain_tag");
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
  EXPECT_FALSE(result.plugin.has_value());
  EXPECT_FALSE(register_called);
  // The diagnostic must name both the plugin's and the host's tag.
  EXPECT_NE(result.error_message.find("toolchain"), std::string::npos)
      << "Expected 'toolchain' in error: " << result.error_message;
  EXPECT_NE(result.error_message.find("test-mismatched-toolchain-tag"),
            std::string::npos)
      << "Expected plugin tag in error: " << result.error_message;
  EXPECT_NE(result.error_message.find(ORC_SDK_TOOLCHAIN_TAG), std::string::npos)
      << "Expected host tag '" << ORC_SDK_TOOLCHAIN_TAG
      << "' in error: " << result.error_message;
}

// ---------------------------------------------------------------------------
// Fully compatible descriptor (matching versions and toolchain tag)
// ---------------------------------------------------------------------------

TEST(StagePluginLoaderRejectionTest, MatchingToolchainTag_Loads) {
  const auto plugin_path = test_fixture_path("compatible");
  if (!std::filesystem::exists(plugin_path)) {
    GTEST_SKIP() << "Test fixture not built: " << plugin_path.string();
  }

  orc::StagePluginLoader loader;
  const auto result = loader.load_plugin(
      plugin_path.string(),
      [](const std::string&, orc::StagePluginLoader::RegisterStageFactory) {
        return true;
      });

  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_TRUE(result.error_message.empty());
  ASSERT_TRUE(result.plugin.has_value());
  EXPECT_EQ(result.plugin->plugin_id, "test.compatible");
}

}  // namespace orc_unit_test
