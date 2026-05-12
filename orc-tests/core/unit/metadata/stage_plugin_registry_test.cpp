/*
 * File:        stage_plugin_registry_test.cpp
 * Module:      orc-core unit tests
 * Purpose:     Unit tests for stage plugin registry YAML parsing and serialization
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include "../../../orc/core/include/stage_plugin_registry.h"

namespace orc_unit_test {

TEST(StagePluginRegistryTest, parseYamlReadsStructuredRegistryEntries)
{
    const std::string yaml_text = R"yaml(
version: 2
plugins:
  - plugin_id: core.ntsc.source
    plugin_version: 1.2.3
    path: /plugins/libcore_ntsc_source.so
    source_repo_url: https://github.com/simoninns/orc-plugin_core-ntsc-source
    artifact_source: github_release_asset
    release_asset_url: https://example.invalid/core-plugin/releases/download/v1.2.3/orc-plugin_ntsc-comp-source_linux-x86_64.so
    release_tag: v1.2.3
    release_asset_name: orc-plugin_ntsc-comp-source_linux-x86_64.so
    target_platform: linux-x86_64
    local_dev_path: /tmp/dev/liborc-stage-plugin-ntsc-comp-source.so
    enabled: true
    trust_state: trusted
    license_spdx: GPL-3.0-or-later
    is_core_plugin: true
    required_host_abi: 7
)yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(yaml_text, "/tmp/stage-plugins.yaml");

    ASSERT_TRUE(result.warnings.empty());
    ASSERT_EQ(result.registry_path, "/tmp/stage-plugins.yaml");
    ASSERT_EQ(result.entries.size(), 1U);

    const auto& entry = result.entries.front();
    EXPECT_EQ(entry.plugin_id, "core.ntsc.source");
    EXPECT_EQ(entry.plugin_version, "1.2.3");
    EXPECT_EQ(entry.path, "/plugins/libcore_ntsc_source.so");
    EXPECT_EQ(entry.source_repo_url, "https://github.com/simoninns/orc-plugin_core-ntsc-source");
    EXPECT_EQ(entry.artifact_source, "github_release_asset");
    EXPECT_EQ(entry.release_asset_url, "https://example.invalid/core-plugin/releases/download/v1.2.3/orc-plugin_ntsc-comp-source_linux-x86_64.so");
    EXPECT_EQ(entry.release_tag, "v1.2.3");
    EXPECT_EQ(entry.release_asset_name, "orc-plugin_ntsc-comp-source_linux-x86_64.so");
    EXPECT_EQ(entry.target_platform, "linux-x86_64");
    EXPECT_EQ(entry.local_dev_path, "/tmp/dev/liborc-stage-plugin-ntsc-comp-source.so");
    EXPECT_TRUE(entry.enabled);
    EXPECT_EQ(entry.trust_state, "trusted");
    EXPECT_EQ(entry.license_spdx, "GPL-3.0-or-later");
    EXPECT_TRUE(entry.is_core_plugin);
    EXPECT_EQ(entry.required_host_abi, 7U);
}

TEST(StagePluginRegistryTest, parseYamlWarnsWhenRemoteAssetNameMissing)
{
    const std::string yaml_text = R"yaml(
version: 2
plugins:
  - plugin_id: broken.plugin
    artifact_source: github_release_asset
    release_asset_url: https://example.invalid/broken/releases/download/v1.0.0/
    enabled: true
  - plugin_id: usable.plugin
    path: /plugins/libusable.so
    enabled: false
)yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(yaml_text);

    ASSERT_EQ(result.entries.size(), 1U);
    EXPECT_EQ(result.entries.front().plugin_id, "usable.plugin");
    EXPECT_FALSE(result.entries.front().enabled);
    ASSERT_EQ(result.warnings.size(), 1U);
    EXPECT_TRUE(
      result.warnings.front().find("release_asset_name (required for download)") != std::string::npos);
}

TEST(StagePluginRegistryTest, parseYamlUsesLocalDevPathWhenPathMissing)
{
    const std::string yaml_text = R"yaml(
version: 2
plugins:
  - plugin_id: dev.override.plugin
    artifact_source: github_release_asset
    release_asset_url: https://example.invalid/dev/releases/download/v2.0.0/orc-plugin_dev-override_linux-x86_64.so
    release_asset_name: orc-plugin_dev-override_linux-x86_64.so
    local_dev_path: /tmp/dev/liborc-stage-plugin-dev-override.so
    enabled: true
 )yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(yaml_text);

    ASSERT_TRUE(result.warnings.empty());
    ASSERT_EQ(result.entries.size(), 1U);
    EXPECT_EQ(result.entries.front().path, "/tmp/dev/liborc-stage-plugin-dev-override.so");
    EXPECT_EQ(result.entries.front().local_dev_path, "/tmp/dev/liborc-stage-plugin-dev-override.so");
  }

  TEST(StagePluginRegistryTest, parseYamlWarnsOnInvalidReleaseAssetName)
  {
    const std::string yaml_text = R"yaml(
version: 2
plugins:
  - plugin_id: invalid.naming
    path: /plugins/libinvalid.so
    artifact_source: github_release_asset
    release_asset_name: invalid-file-name.so
    enabled: true
 )yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(yaml_text);

    ASSERT_EQ(result.entries.size(), 1U);
    ASSERT_EQ(result.warnings.size(), 1U);
    EXPECT_EQ(
      result.warnings.front(),
      "Registry entry has invalid release_asset_name 'invalid-file-name.so' (expected orc-plugin_<stage-name>_<platform>.<so|dylib|dll>)");
  }

TEST(StagePluginRegistryTest, serializeYamlRoundTripsRegistryEntries)
{
    std::vector<orc::StagePluginRegistryEntry> entries;

    orc::StagePluginRegistryEntry entry;
    entry.plugin_id = "thirdparty.scope.decoder";
    entry.plugin_version = "0.9.0";
    entry.path = "/plugins/libthirdparty_decoder.so";
    entry.source_repo_url = "https://github.com/simoninns/orc-plugin_thirdparty-decoder";
    entry.artifact_source = "github_release_asset";
    entry.release_asset_url = "https://example.invalid/thirdparty-decoder/releases/download/v0.9.0/orc-plugin_thirdparty-decoder_linux-x86_64.so";
    entry.release_tag = "v0.9.0";
    entry.release_asset_name = "orc-plugin_thirdparty-decoder_linux-x86_64.so";
    entry.target_platform = "linux-x86_64";
    entry.local_dev_path = "/tmp/dev/liborc-stage-plugin-thirdparty-decoder.so";
    entry.enabled = true;
    entry.trust_state = "untrusted";
    entry.license_spdx = "MIT";
    entry.is_core_plugin = false;
    entry.required_host_abi = 12;
    entries.push_back(entry);

    const auto yaml_text = orc::StagePluginRegistry::serialize_yaml(entries);
    const auto reparsed = orc::StagePluginRegistry::parse_yaml(yaml_text);

    ASSERT_TRUE(reparsed.warnings.empty());
    ASSERT_EQ(reparsed.entries.size(), 1U);

    const auto& round_tripped = reparsed.entries.front();
    EXPECT_EQ(round_tripped.plugin_id, entry.plugin_id);
    EXPECT_EQ(round_tripped.plugin_version, entry.plugin_version);
    EXPECT_EQ(round_tripped.path, entry.path);
    EXPECT_EQ(round_tripped.source_repo_url, entry.source_repo_url);
    EXPECT_EQ(round_tripped.artifact_source, entry.artifact_source);
    EXPECT_EQ(round_tripped.release_asset_url, entry.release_asset_url);
    EXPECT_EQ(round_tripped.release_tag, entry.release_tag);
    EXPECT_EQ(round_tripped.release_asset_name, entry.release_asset_name);
    EXPECT_EQ(round_tripped.target_platform, entry.target_platform);
    EXPECT_EQ(round_tripped.local_dev_path, entry.local_dev_path);
    EXPECT_EQ(round_tripped.enabled, entry.enabled);
    EXPECT_EQ(round_tripped.trust_state, entry.trust_state);
    EXPECT_EQ(round_tripped.license_spdx, entry.license_spdx);
    EXPECT_EQ(round_tripped.is_core_plugin, entry.is_core_plugin);
    EXPECT_EQ(round_tripped.required_host_abi, entry.required_host_abi);
}

TEST(StagePluginRegistryTest, parseYamlWarnsOnGithubRepoNameWithoutOrcPluginPrefix)
{
    const std::string yaml_text = R"yaml(
version: 2
plugins:
  - plugin_id: invalid.repo.name
    path: /plugins/libinvalid.so
    source_repo_url: https://github.com/simoninns/not-prefixed-plugin
    enabled: true
)yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(yaml_text);

    ASSERT_EQ(result.entries.size(), 1U);
    ASSERT_EQ(result.warnings.size(), 1U);
    EXPECT_EQ(
        result.warnings.front(),
        "Registry entry source_repo_url should reference a repository prefixed with 'orc-plugin_' (found 'not-prefixed-plugin')");
}

TEST(StagePluginRegistryTest, parseYamlAcceptsGithubRepoNameWithOrcPluginPrefix)
{
    const std::string yaml_text = R"yaml(
version: 2
plugins:
  - plugin_id: valid.repo.name
    path: /plugins/libvalid.so
    source_repo_url: https://github.com/simoninns/orc-plugin_valid-stage
    enabled: true
)yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(yaml_text);

    ASSERT_TRUE(result.warnings.empty());
    ASSERT_EQ(result.entries.size(), 1U);
}

TEST(StagePluginRegistryTest, parseYamlAcceptsOlderSchemaVersionWithoutWarning)
{
    const std::string yaml_text = R"yaml(
version: 1
plugins:
  - plugin_id: old.schema.plugin
    path: /plugins/libold-schema-plugin.so
    enabled: true
)yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(yaml_text);

    ASSERT_EQ(result.entries.size(), 1U);
    EXPECT_EQ(result.entries.front().plugin_id, "old.schema.plugin");
    for (const auto& warning : result.warnings) {
        EXPECT_EQ(warning.find("schema version"), std::string::npos);
    }
}
// ---------------------------------------------------------------------------
// Path field handling
// ---------------------------------------------------------------------------
//
// The registry parser stores path values verbatim.  Canonicalisation against
// allowed directories happens at load time in stage_registry.cpp.
// These tests document that contract.

TEST(StagePluginRegistryTest, parseYamlStoresRelativeEscapePathVerbatim)
{
    const std::string yaml_text = R"yaml(
version: 2
plugins:
  - plugin_id: external.traversal.plugin
    path: ../../../../usr/lib/libevil.so
    artifact_source: local_path
    enabled: true
)yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(yaml_text);

    ASSERT_EQ(result.entries.size(), 1U);
    EXPECT_EQ(result.entries.front().path, "../../../../usr/lib/libevil.so");
    EXPECT_EQ(result.entries.front().plugin_id, "external.traversal.plugin");
}

TEST(StagePluginRegistryTest, parseYamlStoresAbsolutePathOutsideNormalDirsVerbatim)
{
    const std::string yaml_text = R"yaml(
version: 2
plugins:
  - plugin_id: absolute.outside.plugin
    path: /tmp/suspicious/libmalicious.so
    artifact_source: local_path
    enabled: true
)yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(yaml_text);

    ASSERT_EQ(result.entries.size(), 1U);
    EXPECT_EQ(result.entries.front().path, "/tmp/suspicious/libmalicious.so");
}

} // namespace orc_unit_test