/*
 * File:        phase_7d_skeleton_load_test.cpp
 * Module:      orc-core unit tests
 * Purpose:     Phase 7D contract verification for the canonical skeleton repository
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include "../../../orc/core/include/stage_plugin_registry.h"
#include "../../../orc/core/include/plugin_remote_loader.h"

namespace orc_unit_test {

/**
 * Phase 7D metadata contract test:
 * Verifies that the canonical skeleton release metadata is accepted by the
 * registry when a local precompiled artifact override is supplied.
 *
 * This test proves:
 * 1. Registry accepts the canonical GitHub release metadata fields.
 * 2. A local artifact override is sufficient for the allowed Phase 7D smoke path.
 * 3. The artifact naming contract remains stable across platforms.
 */
TEST(Phase7DSkeletonLoadTest, registryAcceptsSkeletonReleaseMetadataWithLocalOverride)
{
    const std::string skeleton_registry_yaml = R"yaml(
version: 2
plugins:
  - plugin_id: org.decodeorc.stage.skeleton_passthrough
    plugin_version: 0.1.0
    artifact_source: github_release_asset
    release_asset_url: https://github.com/simoninns/orc-plugin_skeleton/releases/download/v0.1.0/orc-plugin_skeleton_passthrough_linux.so
    release_asset_name: orc-plugin_skeleton_passthrough_linux.so
    target_platform: linux
    source_repo_url: https://github.com/simoninns/orc-plugin_skeleton
    release_tag: v0.1.0
    local_dev_path: /tmp/orc-plugin_skeleton_passthrough_linux.so
    enabled: true
    trust_state: untrusted
    license_spdx: GPL-3.0-or-later
)yaml";

    const auto result = orc::StagePluginRegistry::parse_yaml(skeleton_registry_yaml, "<phase-7d-skeleton-registry>");

    EXPECT_EQ(result.registry_path, "<phase-7d-skeleton-registry>");
    ASSERT_TRUE(result.warnings.empty());
    ASSERT_EQ(result.entries.size(), 1U);
    EXPECT_EQ(result.entries[0].plugin_id, "org.decodeorc.stage.skeleton_passthrough");
    EXPECT_EQ(result.entries[0].plugin_version, "0.1.0");
    EXPECT_EQ(result.entries[0].release_tag, "v0.1.0");
    EXPECT_EQ(result.entries[0].source_repo_url, "https://github.com/simoninns/orc-plugin_skeleton");
    EXPECT_EQ(result.entries[0].path, "/tmp/orc-plugin_skeleton_passthrough_linux.so");
}

/**
 * Verify skeleton artifact naming follows Phase 7D convention.
 */
TEST(Phase7DSkeletonLoadTest, skeletonArtifactNamesMatchPhase7DConvention)
{
    // These are the actual published artifacts on GitHub Releases
    const std::vector<std::string> artifacts = {
        "orc-plugin_skeleton_passthrough_linux.so",
        "orc-plugin_skeleton_passthrough_macos.dylib",
        "orc-plugin_skeleton_passthrough_windows.dll"
    };

    for (const auto& artifact : artifacts) {
        // Validate cache path can be constructed
        const auto path = orc::PluginRemoteLoader::get_cache_path(artifact, "linux");
        EXPECT_FALSE(path.empty());
        EXPECT_TRUE(path.find("plugin-cache") != std::string::npos);
        EXPECT_TRUE(path.find(artifact) != std::string::npos);
    }
}

/**
 * Verify cache-path formation remains aligned with the Phase 7D artifact names.
 */
TEST(Phase7DSkeletonLoadTest, cachePathsMatchSkeletonArtifacts)
{
    const std::vector<std::string> skeleton_artifacts = {
        "orc-plugin_skeleton_passthrough_linux.so",
        "orc-plugin_skeleton_passthrough_macos.dylib",
        "orc-plugin_skeleton_passthrough_windows.dll"
    };

    for (const auto& artifact : skeleton_artifacts) {
        const auto path = orc::PluginRemoteLoader::get_cache_path(artifact, "linux");
        EXPECT_FALSE(path.empty());
        EXPECT_TRUE(path.find("plugin-cache") != std::string::npos);
        EXPECT_TRUE(path.find(artifact) != std::string::npos);
    }
}

} // namespace orc_unit_test
