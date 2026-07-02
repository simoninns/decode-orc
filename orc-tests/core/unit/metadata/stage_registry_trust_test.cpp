/*
 * File:        stage_registry_trust_test.cpp
 * Module:      orc-core unit tests
 * Purpose:     Unit tests for registry trust enforcement before plugin load
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include "../../../orc/core/include/stage_registry.h"

namespace orc_unit_test {

namespace {

orc::StagePluginRegistryEntry make_entry(const std::string& plugin_id,
                                         const std::string& path,
                                         const std::string& trust_state,
                                         bool is_core_plugin = false) {
  orc::StagePluginRegistryEntry entry;
  entry.plugin_id = plugin_id;
  entry.path = path;
  entry.trust_state = trust_state;
  entry.is_core_plugin = is_core_plugin;
  entry.enabled = true;
  return entry;
}

}  // namespace

TEST(StageRegistryTrustTest, Collect_SkipsUntrustedEntryWithDiagnostic) {
  std::vector<orc::StagePluginRegistryEntry> entries = {
      make_entry("untrusted.plugin", "/plugins/libuntrusted.so", "untrusted")};
  std::vector<orc::StagePluginDiagnostic> diagnostics;

  const auto paths =
      orc::collect_trusted_registry_plugin_paths(entries, diagnostics);

  EXPECT_TRUE(paths.empty());
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().severity,
            orc::StagePluginDiagnosticSeverity::Warning);
  EXPECT_EQ(diagnostics.front().path, "/plugins/libuntrusted.so");
  EXPECT_TRUE(diagnostics.front().message.find("untrusted.plugin") !=
              std::string::npos);
  EXPECT_TRUE(diagnostics.front().message.find("was not loaded") !=
              std::string::npos);
}

TEST(StageRegistryTrustTest, Collect_IncludesTrustedEntry) {
  std::vector<orc::StagePluginRegistryEntry> entries = {
      make_entry("trusted.plugin", "/plugins/libtrusted.so", "trusted")};
  std::vector<orc::StagePluginDiagnostic> diagnostics;

  const auto paths =
      orc::collect_trusted_registry_plugin_paths(entries, diagnostics);

  ASSERT_EQ(paths.size(), 1U);
  EXPECT_EQ(paths.front(), "/plugins/libtrusted.so");
  EXPECT_TRUE(diagnostics.empty());
}

TEST(StageRegistryTrustTest, Collect_CoreEntryLoadsRegardlessOfTrustState) {
  std::vector<orc::StagePluginRegistryEntry> entries = {
      make_entry("core.plugin", "/plugins/libcore.so", "untrusted",
                 /*is_core_plugin=*/true)};
  std::vector<orc::StagePluginDiagnostic> diagnostics;

  const auto paths =
      orc::collect_trusted_registry_plugin_paths(entries, diagnostics);

  ASSERT_EQ(paths.size(), 1U);
  EXPECT_EQ(paths.front(), "/plugins/libcore.so");
  EXPECT_TRUE(diagnostics.empty());
}

TEST(StageRegistryTrustTest, Collect_SkipsDisabledEntriesSilently) {
  auto entry =
      make_entry("disabled.plugin", "/plugins/libdisabled.so", "untrusted");
  entry.enabled = false;
  std::vector<orc::StagePluginRegistryEntry> entries = {entry};
  std::vector<orc::StagePluginDiagnostic> diagnostics;

  const auto paths =
      orc::collect_trusted_registry_plugin_paths(entries, diagnostics);

  EXPECT_TRUE(paths.empty());
  EXPECT_TRUE(diagnostics.empty());
}

TEST(StageRegistryTrustTest, Collect_MixedEntriesFilterIndependently) {
  std::vector<orc::StagePluginRegistryEntry> entries = {
      make_entry("trusted.plugin", "/plugins/liba.so", "trusted"),
      make_entry("untrusted.plugin", "/plugins/libb.so", "untrusted"),
      make_entry("core.plugin", "/plugins/libc.so", "untrusted",
                 /*is_core_plugin=*/true)};
  std::vector<orc::StagePluginDiagnostic> diagnostics;

  const auto paths =
      orc::collect_trusted_registry_plugin_paths(entries, diagnostics);

  ASSERT_EQ(paths.size(), 2U);
  EXPECT_EQ(paths[0], "/plugins/liba.so");
  EXPECT_EQ(paths[1], "/plugins/libc.so");
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_TRUE(diagnostics.front().message.find("untrusted.plugin") !=
              std::string::npos);
}

}  // namespace orc_unit_test
